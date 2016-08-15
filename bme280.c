/*
 * Driver for the BME280 pressure sensor.
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
#include "bmp280/bmp280.h"
#include "ds3231/ds3231.h"

#include <espressif/esp_misc.h>
#include "espressif/esp8266/gpio_register.h"

#include "buffer.h"
#include "i2c.h"
#include "leds.h"



static void bme280_read_task(void *pvParameters)
{
    /* Delta encoding state. */
    uint32_t last_index = 0;
    uint32_t last_bme280_temp = 0;
    uint32_t last_bme280_pressure = 0;
    uint32_t last_bme280_humidity = 0;

    xSemaphoreTake(i2c_sem, portMAX_DELAY);

    bmp280_params_t bme280_params;
    bmp280_init_default_params(&bme280_params);
    bme280_params.mode = BMP280_MODE_NORMAL;
    bme280_params.filter = BMP280_FILTER_16;
    bme280_params.oversampling = BMP280_ULTRA_HIGH_RES;
    bme280_params.standby = BMP280_STANDBY_250;

    bmp280_calib_t bme280_calib;
    bme280_calib.i2c_addr = BMP280_I2C_ADDRESS_0;
    bool bme280_available = bmp280_init(&bme280_calib, &bme280_params);
    xSemaphoreGive(i2c_sem);

    if (!bme280_available)
        vTaskDelete(NULL);

    bool bme280p = bme280_calib.id == BME280_CHIP_ID;
    
    for (;;) {
        vTaskDelay(10000 / portTICK_RATE_MS);

        xSemaphoreTake(i2c_sem, portMAX_DELAY);

        int32_t temperature;
        uint32_t pressure;
        uint32_t humidity = 0;
        if (!bmp280_read_fixed(&bme280_calib, &temperature, &pressure,
                               bme280p ? &humidity : NULL)) {
            xSemaphoreGive(i2c_sem);
            blink_red();
            continue;
        }

        xSemaphoreGive(i2c_sem);

        while (1) {
            uint8_t outbuf[15];
            /* Delta encoding */
            int32_t temp_delta = (int32_t)temperature - (int32_t)last_bme280_temp;
            uint32_t len = emit_leb128_signed(outbuf, 0, temp_delta);
            int32_t pressure_delta = (int32_t)pressure - (int32_t)last_bme280_pressure;
            len = emit_leb128_signed(outbuf, len, pressure_delta);
            int32_t code = DBUF_EVENT_BMP280_TEMP_PRESSURE;

            if (bme280p) {
                int32_t humidity_delta = (int32_t)humidity - (int32_t)last_bme280_humidity;
                len = emit_leb128_signed(outbuf, len, humidity_delta);
                code = DBUF_EVENT_BME280_TEMP_PRESSURE_RH;
            }

            uint32_t new_index = dbuf_append(last_index, code, outbuf, len, 1, 0);
            if (new_index == last_index)
                break;

            /* Moved on to a new buffer. Reset the delta encoding
             * state and retry. */
            last_index = new_index;
            last_bme280_temp = 0;
            last_bme280_pressure = 0;
            last_bme280_humidity = 0;
        };

        blink_green();

        /*
         * Commit the values logged. Note this is the only task
         * accessing this state so these updates are synchronized with
         * the last event of this class append.
         */
        last_bme280_temp = temperature;
        last_bme280_pressure = pressure;
        last_bme280_humidity = humidity;
    }
}



void init_bme280()
{
    xTaskCreate(&bme280_read_task, (signed char *)"bme280_read_task", 256, NULL, 2, NULL);
}
