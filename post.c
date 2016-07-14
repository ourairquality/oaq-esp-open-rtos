/*
 * HTTP-Post the flash sectors to a server.
 *
 * Licensed under the Apache License, Version 2.0, January 2004 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
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
 */
#include "espressif/esp_common.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "ssid_config.h"

#include "buffer.h"
#include "flash.h"
#include "sha3.h"

#define WEB_SERVER "ourairquality.org"
#define WEB_PORT "80"
#define SENSOR_ID "pm3003a"
#define WEB_PATH "/sensors/"SENSOR_ID"/data"

/* For signaling and waiting for data to post. */
xSemaphoreHandle post_data_sem = NULL;

/*
 * A single buffer is allocated to hold the HTTP data to be sent and it is large
 * enough for the HTTP header plus the content including a signature suffix. The
 * content is located at a fixed position into the buffer and word aligned so
 * the flash data can be copied directly to this buffer. For the computation of
 * a MAC-SHA3 signature the key prefix is copied before the data so the prefix
 * allocation needs to be large enough for this too.
 */

#define KEY_SIZE 287
#define SIGNATURE_SIZE 28
static uint8_t sha3_key[KEY_SIZE] =
{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 
 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 
 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 
 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 
 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 
 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 
 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 
 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 
 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 
 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 
 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 
 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 
 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 
 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF, 
 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E};

#define PREFIX_SIZE 288 /* At least KEY_SIZE */
#define POST_BUFFER_SIZE (PREFIX_SIZE + 4 + 4 + 4 + 4096 + SIGNATURE_SIZE)
static uint8_t post_buf[POST_BUFFER_SIZE];

static void post_data_task(void *pvParameters)
{
    uint32_t last_index = 0;

    while (1) {
        xSemaphoreTake(post_data_sem, 5000 / portTICK_RATE_MS);

        /* Try to flush all the pending buffers before waiting again. */
        while (1) {
            uint32_t start, index;
            int j;

            /* Lightweight check if there is anything to post. */
            if (!maybe_buffer_to_post())
                break;

            /* Try connecting to the server before requesting the actual data to
             * post as this might take some time to succeed and by then there
             * might be much more data to send. Also a time is sent and the
             * intention is that it is as close to the time posted as possible
             * and it can not be patched in just before sending as it is part of
             * the signed message.
             */
            
            const struct addrinfo hints = {
                .ai_family = AF_INET,
                .ai_socktype = SOCK_STREAM,
            };
            struct addrinfo *res;

            int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
            if (err != 0 || res == NULL) {
                if (res)
                    freeaddrinfo(res);
                vTaskDelay(1000 / portTICK_RATE_MS);
                continue;
            }

            int s = socket(res->ai_family, res->ai_socktype, 0);
            if (s < 0) {
                freeaddrinfo(res);
                vTaskDelay(1000 / portTICK_RATE_MS);
                continue;
            }

            if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
                close(s);
                freeaddrinfo(res);
                vTaskDelay(4000 / portTICK_RATE_MS);
                continue;
            }

            freeaddrinfo(res);

            /* The buffer to copy the data into needs to be aligned as the read
             * of the flash copies directly into it. */
            uint32_t size = get_buffer_to_post(&index, &start, &post_buf[PREFIX_SIZE + 12]);

            if (size == 0) {
                close(s);
                break;
            }

            /*
             * The index of the record, and the index at which this content
             * starts within the record are prefixed.
             */

            uint32_t time = RTC.COUNTER;
            post_buf[PREFIX_SIZE + 0] = time;
            post_buf[PREFIX_SIZE + 1] = time >>  8;
            post_buf[PREFIX_SIZE + 2] = time >> 16;
            post_buf[PREFIX_SIZE + 3] = time >> 24;

            post_buf[PREFIX_SIZE + 4] = index;
            post_buf[PREFIX_SIZE + 5] = index >>  8;
            post_buf[PREFIX_SIZE + 6] = index >> 16;
            post_buf[PREFIX_SIZE + 7] = index >> 24;

            post_buf[PREFIX_SIZE + 8] = start;
            post_buf[PREFIX_SIZE + 9] = start >>  8;
            post_buf[PREFIX_SIZE + 10] = start >> 16;
            post_buf[PREFIX_SIZE + 11] = start >> 24;

            /*
             * Firstly use the prefix area for the key to implement MAC-SHA3.
             */
            memcpy(&post_buf[PREFIX_SIZE - KEY_SIZE], sha3_key, KEY_SIZE);
            FIPS202_SHA3_224(&post_buf[PREFIX_SIZE - KEY_SIZE],
                             KEY_SIZE + 12 + size,
                             &post_buf[PREFIX_SIZE + 12 + size]);

            /*
             * Next use the prefix area for the HTTP header.
             */
            uint32_t header_size = snprintf(post_buf, PREFIX_SIZE,
                                            "POST %s HTTP/1.1\r\n"
                                            "Host: %s:%s\r\n"
                                            "Connection: close\r\n"
                                            "Content-Type: application/octet-stream\r\n"
                                            "Content-Length: %d\r\n"
                                            "\r\n", WEB_PATH, WEB_SERVER, WEB_PORT,
                                            12 + size + SIGNATURE_SIZE);
            /*
             * Move the header up to meet the data.
             */
            for (j = 0; j < header_size; j++)
                post_buf[PREFIX_SIZE - j - 1] = post_buf[header_size - j - 1];

            /*
             * Data ready to send.
             */
            if (write(s, &post_buf[PREFIX_SIZE - header_size],
                      header_size + 12 + size + SIGNATURE_SIZE) < 0) {
                close(s);
                vTaskDelay(4000 / portTICK_RATE_MS);
                continue;
            }

            char recv_buf[128];
            int r;
            /* Search for the end of the response headers */
            int end = 0;
            int headers_end = 0;
            int i;
            do {
                r = read(s, recv_buf + end, 127 - end);
                if (r > 0) {
                    end += r;
                    if (end >= 4) {
                        for (i = 0; i < end - 4; i++) {
                            if (recv_buf[i + 0] == '\r' &&
                                recv_buf[i + 1] == '\n' &&
                                recv_buf[i + 2] == '\r' &&
                                recv_buf[i + 3] == '\n') {
                                headers_end = i + 4;
                                break;
                            }
                        }
                        if (headers_end)
                            break;
                        /* Keep the last three bytes as they might have
                         * contained part of the end of the headers. */
                        for (i = 0; i < 3; i++)
                            recv_buf[i] = recv_buf[end - 3 + i];
                        end = 3;
                    }
                }
            } while (r > 0);

            if (headers_end) {
                /* Shift down. */
                end -= headers_end;
                for (i = 0; i < end; i++)
                    recv_buf[i] = recv_buf[headers_end + i];
                /* Keep reading the body. */
                while (end < 20) {
                    r = read(s, recv_buf + end, 127 - end);
                    if (r < 0)
                        break;
                    end += r;
                };

                /* Accept larger responses, for future extension. There is a
                 * magic number that indicates a successful response which is
                 * checked. */
                if (end >= 20) {
                    uint32_t recv_magic = recv_buf[0] |
                        (recv_buf[1] << 8) |
                        (recv_buf[2] << 16) |
                        (recv_buf[3] << 24);
                    uint32_t recv_sec = recv_buf[4] |
                        (recv_buf[5] << 8) |
                        (recv_buf[6] << 16) |
                        (recv_buf[7] << 24);
                    uint32_t recv_usec = recv_buf[8] |
                        (recv_buf[9] << 8) |
                        (recv_buf[10] << 16) |
                        (recv_buf[11] << 24);
                    uint32_t recv_index = recv_buf[12] |
                        (recv_buf[13] << 8) |
                        (recv_buf[14] << 16) |
                        (recv_buf[15] << 24);
                    uint32_t recv_size = recv_buf[16] |
                        (recv_buf[17] << 8) |
                        (recv_buf[18] << 16) |
                        (recv_buf[19] << 24);

                    if (recv_magic == 0x70F55EA8) {
                        /* Log the server time in it's response. This gives time
                         * stamps to the events logged to help synchronize the
                         * RTC counter to the real time. While the server could
                         * log the times to synchronize to the RTC counter, this
                         * gives some resilience against server data loss and
                         * allows the sectors records to stand on their own.
                         *
                         * The event time-stamp is close enough to the received
                         * time, and include the posted time too to allow
                         * matching with the server recorded times and also to
                         * give the round-trip time to send and receive the post
                         * which might help estimate the accuracy. Re-use the
                         * post_buf to build this event, the RTC time should
                         * still be there.
                         */
                        post_buf[PREFIX_SIZE + 4] = recv_sec;
                        post_buf[PREFIX_SIZE + 5] = recv_sec >>  8;
                        post_buf[PREFIX_SIZE + 6] = recv_sec >> 16;
                        post_buf[PREFIX_SIZE + 7] = recv_sec >> 24;

                        post_buf[PREFIX_SIZE + 8] = recv_usec;
                        post_buf[PREFIX_SIZE + 9] = recv_usec >>  8;
                        post_buf[PREFIX_SIZE + 10] = recv_usec >> 16;
                        post_buf[PREFIX_SIZE + 11] = recv_usec >> 24;

                        while (1) {
                            /* Flag this for high precision time (no
                             * truncation), and to skip logging if the immediate
                             * prior event is the same. */
                            uint32_t new_index = dbuf_append(last_index,
                                                             DBUF_EVENT_POST_TIME,
                                                             &post_buf[PREFIX_SIZE],
                                                             12, 0, 1);
                            if (new_index == last_index)
                                break;
                            last_index = new_index;
                        }

                        /* The server response is used to set the buffer indexes
                         * known to have been received. This allows the server
                         * to request data be re-sent, or to skip over data
                         * already received when restarted. A bad index or size
                         * is handled when searching for the next buffer to post
                         * so does not need limiting here. */
                        note_buffer_posted(recv_index, recv_size);
                    }
                    close(s);
                    continue;
                }
            }

            close(s);

            for (int countdown = 10; countdown >= 0; countdown--) {
                vTaskDelay(1000 / portTICK_RATE_MS);
            }
        }
    }
}

void init_network()
{
    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };

    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);
    sdk_wifi_set_sleep_type(WIFI_SLEEP_MODEM);

    vSemaphoreCreateBinary(post_data_sem);
    xTaskCreate(&post_data_task, (signed char *)"post_task", 480, NULL, 1, NULL);
}
