/*
 * Particle counter data logger.
 * Memory resident (RAM) buffer support.
 *
 * Licensed under the Apache License, Version 2.0, January 2004 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *      http://www.apache.org/licenses/
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS WITH THE SOFTWARE.
 *
 *
 * Data is collected in 4096 byte buffers, the same size as the flash sectors,
 * and then saved to the flash storage, and then posted to a server.
 *
 * Separate tasks: 1. read the UART appending events to a ring of memory
 * resident buffers; 2. save these buffers to flash sectors; 3. HTTP-Post these
 * sectors to a server.
 *
 * Within each buffer the values may be compressed, but each buffer stands
 * alone.
 *
 * Unused bytes in the buffer are filled with ones bits to support writing to
 * flash multiple times for saving partial buffers. The buffer encoding must
 * allow recovery of the entries from these ones-filled buffers, which requires
 * that each entry have at least one zero bit.
 *
 * Events added to the buffer have a time stamp which is delta encoded. These
 * are not required to be real times and are expected to be synchronized to
 * external events such as successful posts to a server.
 */

#include "espressif/esp_common.h"
#include "espressif/esp_system.h"

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <esp/uart.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "buffer.h"
#include "leds.h"
#include "flash.h"
#include "post.h"
#include "pms.h"
#include "i2c.h"
#include "sht21.h"
#include "bmp180.h"
#include "ds3231.h"


#define DBUF_DATA_SIZE 4096

typedef struct {
    /* The size of the filled data bytes. */
    uint32_t size;
    /* The size that has been saved successfully. */
    uint32_t save_size;
    /* Time-stamp of the first event written to the buffer after the last save,
     * or the time of the oldest event not saved. */
    uint32_t write_time;
    /* The data. Initialized to all ones bits (0xff). The first two 32 bit words
     * are an unique index that is monotonically increasing. The second copy is
     * for redundancy and is inverted to help catch errors when saved to
     * flash. */
    uint8_t data[DBUF_DATA_SIZE];
} dbuf_t;

/*
 * The buffers are managed as a ring-buffer. If the oldest data is not saved in
 * time then it is discarded.
 */

#define NUM_DBUFS 4

static dbuf_t dbufs[NUM_DBUFS];

/* The current data is written to the dbufs_head buffer. */
static uint32_t dbufs_head;
/* The oldest buffer is the dbufs_tail, which is equal to the dbufs_head if
 * there is only one live buffer. */
static uint32_t dbufs_tail;

/* To synchronize access to the data buffers. */
static xSemaphoreHandle dbufs_sem;

/* Return the index for the buffer number. */
static uint32_t dbuf_index(uint32_t num)
{
    uint32_t *words = (uint32_t *)(dbufs[num].data);
    uint32_t index = words[0];
    return index;
}

uint32_t dbuf_head_index() {
    xSemaphoreTake(dbufs_sem, portMAX_DELAY);
    uint32_t index = dbuf_index(dbufs_head);
    xSemaphoreGive(dbufs_sem);
    return index;
}

static void set_dbuf_index(uint32_t num, uint32_t index)
{
    uint32_t *words = (uint32_t *)(dbufs[num].data);
    words[0] = index;
    words[1] = index ^ 0xffffffff;
}

static void initialize_dbuf(uint32_t num)
{
    dbuf_t *dbuf = &dbufs[num];
    dbuf->size = 0;
    dbuf->save_size = 0;
    uint32_t time = RTC.COUNTER;
    dbuf->write_time = time;
    memset(dbuf->data, 0xff, DBUF_DATA_SIZE);
}

/* Emit an unsigned leb128. */
uint32_t emit_leb128(uint8_t *buf, uint32_t start, uint64_t v)
{
    while (1) {
        if (v < 0x80) {
            buf[start++] = v;
            return start;
        }
        buf[start++] = (v & 0x7f) | 0x80;
        v >>= 7;
    }
}

/* Emit a signed leb128. */
uint32_t emit_leb128_signed(uint8_t *buf, uint32_t start, int32_t v)
{
    while (1) {
        if (-0x40 <= v && v <= 0x3f) {
            buf[start++] = v & 0x7f;
            return start;
        }
        buf[start++] = (v & 0x7f) | 0x80;
        v >>= 7;
    }
}

/*
 * Append an event to the buffers. This function firstly emits the common event
 * header including the event code, size, and the time stamp using the RTC
 * counter.
 *
 * Emitting the code and length here supports skipping unrecognized events.
 *
 * Emitting the time here keeps the times increasing, whereas if the caller
 * emitted the time then multiple callers might race to append their events and
 * the times might not be in order.
 *
 * When the low_res_time option is selected some of the time low bits are
 * allowed to be zeroed, effectively moving the event back in time a
 * little. This can support a more compact encoding for the time. The time is
 * only truncated when it does not cause a backward step in time since the last
 * time-stamp.
 *
 * If the no_repeat flag is set and the code and size are the same as the
 * immediately prior event in the same buffer then no event is logged. This is
 * used to log the times in responses from a server, to prevent an ongoing log
 * to the server of only the server responses.
 *
 * The append operation might fail if there is not room, and the caller is
 * expected to retry. Each buffer stands alone, so delta encoding needs to be
 * reset for each new buffer, and if the delta encoding changes then the encoded
 * data size might change too so the caller needs to re-encode the event
 * data. The called needs to know when the buffer has changed to reset the state
 * and to do this the index is passed in an if not the current index then the
 * append abort, the current index is returned.
 */
static int32_t last_code;
static int32_t last_size;
static uint32_t last_time;

uint32_t dbuf_append(uint32_t index, uint16_t code, uint8_t *data, uint32_t size,
                     int low_res_time, int no_repeat)
{
    xSemaphoreTake(dbufs_sem, portMAX_DELAY);

    uint32_t current_index = dbuf_index(dbufs_head);
    if (index != current_index) {
        /* Moved on to the next buffer. The caller must reset any delta encoding
         * state and retry. */
        xSemaphoreGive(dbufs_sem);
        return current_index;
    }

    uint32_t time = RTC.COUNTER;
    /* Space to emit the code, size, and time. */
    uint8_t header[15];
    uint32_t header_size = 0;

    if (low_res_time) {
        /* Protect against stepping backwards in time, which would look like
         * wrapping which would be a big step forward in time. If the low bits
         * of the last_time are zero then truncating the current time low bits
         * can not step backwards. If the significant bits of the last_time and
         * current time are not equal then it is also safe. */
        if ((last_time & 0x00001fff) == 0 ||
            (last_time & 0xffffe000) != (time & 0xffffe000)) {
            /* Truncate the time, don't need all the precision. Note that the
             * time delta low bits will not necessarily be zero for this event,
             * but if the following event also uses a low_res_time then the time
             * delta low bits will be zero then. */
            time = time & 0xffffe000;
        }
    }

    /* The time is always at least delta encoded, mod32. */
    uint32_t time_delta = time - last_time;

    /* The first two bits, the two lsb, encode the header format.
     *
     * Bit 0:
     *   0 = same code and size as last event.
     *   1 = leb128 event code (two low bits removed), and size.
     *
     * Bit 1:
     *   0 = leb128 time delta.
     *   1 = leb128 truncated time delta.
     *       
     * The event code must have one zero bit in the first 5 bits to ensure that
     * the first byte always has one zero bit if there is an event, and that
     * 0xff terminates the event log.
     */

    if (code == last_code && size == last_size) {
        if (no_repeat) {
            xSemaphoreGive(dbufs_sem);
            return index;
        }
        
        if ((time_delta & 0x00001fff) == 0) {
            uint64_t v = time_delta >> (13 - 2) | 2 | 0;
            header_size = emit_leb128(header, header_size, v);
        } else {
            uint64_t v = (uint64_t)time_delta << 2UL | 0 | 0;
            header_size = emit_leb128(header, header_size, v);
        }
    } else {
        if ((time_delta & 0x00001fff) == 0) {
            header_size = emit_leb128(header, header_size, (code << 2) | 2 | 1);
            header_size = emit_leb128(header, header_size, size);
            uint64_t v = time_delta >> 13;
            header_size = emit_leb128(header, header_size, v);
        } else {
            header_size = emit_leb128(header, header_size, (code << 2) | 0 | 1);
            header_size = emit_leb128(header, header_size, size);
            uint64_t v = (uint64_t)time_delta;
            header_size = emit_leb128(header, header_size, v);
        }
    }

    uint32_t total_size = header_size + size;

    /* Check if there is room in the current buffer. */
    dbuf_t *head = &dbufs[dbufs_head];
    if (total_size > DBUF_DATA_SIZE - 8) {
        xSemaphoreGive(dbufs_sem);
        /* Consume it to clear the error. */
        return index;
    }
    if (head->size + total_size > DBUF_DATA_SIZE) {
        /* Full, move to the next buffer. */
        index++;
        /* Reuse the head buffer if it is the only active buffer and its data
         * has been saved. This check prevents a saved buffer being retained
         * which would break an assumed invariant. */
        if (dbufs_head != dbufs_tail || head->size != head->save_size) {
            /* Can not reuse the head buffer. */
            dbufs_head++;
            if (dbufs_head >= NUM_DBUFS)
                dbufs_head = 0;
            if (dbufs_head == dbufs_tail) {
                /* Wrapped, discard the tail buffer. */
                dbufs_tail++;
                if (dbufs_tail >= NUM_DBUFS)
                    dbufs_tail = 0;
            }
            head = &dbufs[dbufs_head];
        }
        initialize_dbuf(dbufs_head);
        set_dbuf_index(dbufs_head, index);
        head->size = 8;
        /* Reset the prior-event state. */
        last_code = 0;
        last_size = 0;
        last_time = 0;
        xSemaphoreGive(dbufs_sem);
        /* Caller must reset any delta encoding state and retry. */
        return index;
    }

    /* Reset the write time if this is the first real write to the buffer, or
     * the first write since the last save. This prevents an immediate or early
     * save of new content added. */
    if (head->size <= 8 || head->size == head->save_size)
        head->write_time = time;

    /* Emit the event header. */
    uint32_t i;
    for (i = 0; i < header_size; i++)
        head->data[head->size + i] = header[i];
    /* Emit the event data. */
    for (i = 0; i < size; i++)
        head->data[head->size + header_size + i] = data[i];

    head->size += total_size;

    last_code = code;
    last_size = size;
    last_time = time;

    xSemaphoreGive(dbufs_sem);

    /* Wakeup the flash_data_task. */
    xSemaphoreGive(flash_data_sem);
    return index;
}
    
/*
 * Search for a buffer to write to flash. Fill the target buffer and return the
 * size currently used if there is something to send, otherwise return zero. If
 * some of the buffer has been successfully posted then the start of the
 * non-written elements is set. The full buffer is always copied, to get the
 * trailing ones, and because the flash write might fail and the entire buffer
 * might need to be re-written to the next flash sector.
 *
 * The buffers are always returned in the order of their index, so this starts
 * searching at the tail of the buffer FIFO, and if nothing else then see if the
 * current buffer could be usefully saved.
 *
 * A copy of the buffer is made to allow the dbufs_sem to be released quickly.
 *
 * On success note_buffer_written() should be called to allow the buffer to be
 * freed and reused, and the index is at the head of the buffer.
 *
 * It is assumed that the memory resident buffers are saved well before the RTC
 * time used here can wrap.
 */

uint32_t get_buffer_to_write(uint8_t *buf, uint32_t *start)
{
    uint32_t size = 0;

    xSemaphoreTake(dbufs_sem, portMAX_DELAY);

    if (dbufs_tail != dbufs_head) {
        dbuf_t *dbuf = &dbufs[dbufs_tail];
        if (dbuf->size > dbuf->save_size) {
            uint32_t j;
            size = dbuf->size;
            for (j = 0; j < DBUF_DATA_SIZE; j++)
                buf[j] = dbuf->data[j];
            *start = dbuf->save_size;
            xSemaphoreGive(dbufs_sem);
            return size;
        }
        xSemaphoreGive(dbufs_sem);
        return 0;
    }

    /* Otherwise check if the head buffer needs to be saved.  Don't bother
     * saving a sector with only an index. */
    dbuf_t *head = &dbufs[dbufs_head];
    if (head->size > 8 && head->size > head->save_size) {
        uint32_t delta = RTC.COUNTER - head->write_time;
        // Currently about 120 seconds.
        if (delta > 20000000) {
            uint32_t j;
            size = head->size;
            for (j = 0; j < DBUF_DATA_SIZE; j++)
                buf[j] = head->data[j];
            *start = head->save_size;
            xSemaphoreGive(dbufs_sem);
            return size;
        }
    }

    xSemaphoreGive(dbufs_sem);
    return 0;
}

/*
 * Callback to note that a buffer has been successfully written to flash
 * storage. The buffer index is passed in to locate the buffer. The size is
 * passed in to update the successfully saved size, and is the total size saved,
 * not an incremental update.
 *
 * The buffer might have been filled more and even moved from being the head to
 * a non-head buffer, so the size is used to check if all the buffer has been
 * saved and only then can it be freed. The head buffer is never freed as it
 * likely has room for more events.
 *
 * The buffer might have wrapped and been re-used, in which case it has already
 * been freed.
 */
void note_buffer_written(uint32_t index, uint32_t size)
{
    xSemaphoreTake(dbufs_sem, portMAX_DELAY);

    uint32_t i = dbufs_tail;
    while (1) {
        if (dbuf_index(i) == index)
            break;
        if (i == dbufs_head) {
            /* Did not find the index, possibly wrapped already so give up. */
            xSemaphoreGive(dbufs_sem);
            return;
        }
        if (++i >= NUM_DBUFS)
            i = 0;
    }

    /* Update the save_size */
    dbufs[i].save_size = size;

    /* Update the write_time here. More content might have been saved so this
     * might be a little late for some content, but that would only delay the
     * next write a little. If this is not a head buffer then the time is not
     * even used, rather just the save_size. */
    dbufs[i].write_time = RTC.COUNTER;

    /* Free saved buffers from the tail. */
    for (; dbufs_tail != dbufs_head; ) {
        dbuf_t *dbuf = &dbufs[dbufs_tail];
        if (dbuf->save_size == dbuf->size) {
            dbufs_tail++;
            if (dbufs_tail >= NUM_DBUFS)
                dbufs_tail = 0;
        } else {
            break;
        }
    }

    xSemaphoreGive(dbufs_sem);
    blink_blue();
}



void user_init(void)
{
    uart_set_baud(0, 9600);

    init_i2c();

    dbufs_head = 0;
    dbufs_tail = 0;
    initialize_dbuf(dbufs_head);
    uint32_t last_index = init_flash();
    set_dbuf_index(dbufs_head, last_index);
    dbufs[dbufs_head].size = 8;

    last_code = 0;
    last_size = 0;
    last_time = 0;

    dbufs_sem = xSemaphoreCreateMutex();

    xTaskCreate(&flash_data_task, (signed char *)"flash_task", 256, NULL, 2, NULL);

    /* Log a startup event. */
    uint32_t startup[8 + 1];
    /* Include the SDK reset info. */
    struct sdk_rst_info* reset_info = sdk_system_get_rst_info();
    memcpy(startup, reset_info, sizeof(struct sdk_rst_info));
    /* Include the initial RTC calibration, and average a few calls as it seems
     * rather noisy. */
    startup[8] = 0;
    for (int i = 0; i < 32; i++)
        startup[8] += sdk_system_rtc_clock_cali_proc();
    startup[8] >>= 5;
    while (1) {
        uint32_t new_index = dbuf_append(last_index, DBUF_EVENT_ESP8266_STARTUP,
                                         (void *)startup, sizeof(startup), 1, 0);
        if (new_index == last_index)
            break;
        last_index = new_index;
    }

    init_network();

    init_blink();
    blink_red();
    blink_blue();
    blink_green();

    init_pms();
    init_sht2x();
    init_bmp180();
    init_ds3231();
}
