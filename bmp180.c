/*
 * Driver for the BMP180 pressure sensor.
 *
 * Copyright (C) 2016 OurAirQuality.org
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

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp/uart.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "i2c/i2c.h"
#include "bmp180/bmp180.h"
#include "ds3231/ds3231.h"

#include <espressif/esp_misc.h>
#include "espressif/esp8266/gpio_register.h"

#include "buffer.h"
#include "i2c.h"
#include "leds.h"



static void bmp180_read_task(void *pvParameters)
{
    /* Delta encoding state. */
    uint32_t last_index = 0;
    uint32_t last_bmp180_temp = 0;
    uint32_t last_bmp180_pressure = 0;

    xSemaphoreTake(i2c_sem, portMAX_DELAY);
    bmp180_constants_t bmp180_constants;
    bool bmp180_available = bmp180_is_available() &&
        bmp180_fillInternalConstants(&bmp180_constants);
    xSemaphoreGive(i2c_sem);

    if (!bmp180_available)
        vTaskDelete(NULL);

    for (;;) {
        vTaskDelay(10000 / portTICK_RATE_MS);

        xSemaphoreTake(i2c_sem, portMAX_DELAY);

        int32_t temperature;
        uint32_t pressure;
        if (!bmp180_measure(&bmp180_constants, &temperature, &pressure, 3)) {
            xSemaphoreGive(i2c_sem);
            blink_red();
            continue;
        }
            
        xSemaphoreGive(i2c_sem);

        while (1) {
            uint8_t outbuf[12];
            /* Delta encoding */
            int32_t temp_delta = (int32_t)temperature - (int32_t)last_bmp180_temp;
            uint32_t len = emit_leb128_signed(outbuf, 0, temp_delta);
            int32_t pressure_delta = (int32_t)pressure - (int32_t)last_bmp180_pressure;
            len = emit_leb128_signed(outbuf, len, pressure_delta);
            int32_t code = DBUF_EVENT_BMP180_TEMP_PRESSURE;
            uint32_t new_index = dbuf_append(last_index, code, outbuf, len, 1, 0);
            if (new_index == last_index)
                break;

            /* Moved on to a new buffer. Reset the delta encoding
             * state and retry. */
            last_index = new_index;
            last_bmp180_temp = 0;
            last_bmp180_pressure = 0;
        };

        blink_green();

        /*
         * Commit the values logged. Note this is the only task
         * accessing this state so these updates are synchronized with
         * the last event of this class append.
         */
        last_bmp180_temp = temperature;
        last_bmp180_pressure = pressure;
    }
}



void init_bmp180()
{
    xTaskCreate(&bmp180_read_task, (signed char *)"bmp180_read_task", 256, NULL, 2, NULL);
}
