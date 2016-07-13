/*
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
 */

extern uint32_t get_buffer_to_write(uint8_t *buf, uint32_t *start);
extern void note_buffer_written(uint32_t index, uint32_t size);
extern uint32_t dbuf_append(uint32_t index, uint16_t code, uint8_t *data, uint32_t size,
                            int low_res_time, int no_repeat);

/* Plantower PMS3003 */
#define DBUF_EVENT_PMS3003 1

/* Plantower PMS1003 PMS5003 PMS7003 */
#define DBUF_EVENT_PMS5003 2

#define DBUF_EVENT_POST_TIME 3

#define DBUF_EVENT_ESP8266_STARTUP 4
