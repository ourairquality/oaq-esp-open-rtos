/*
 * Configuration parameters.
 *
 * Copyright (C) 2016 OurAirQuality.org
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

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "sysparam.h"

/*
 * Parameters.
 */
uint8_t param_board;
uint8_t param_pms_uart;
uint8_t param_i2c_scl;
uint8_t param_i2c_sda;
char *param_web_server;
char *param_web_port;
char *param_web_path;
uint32_t param_sensor_id;
uint32_t param_key_size;
uint8_t *param_sha3_key;

void init_params()
{
    sysparam_status_t status;

    param_board = 0;
    param_pms_uart = 0;
    param_i2c_scl = 0;
    param_i2c_sda = 2;
    param_web_server = NULL;
    param_web_port = NULL;
    param_web_path = NULL;
    param_sensor_id = 0;
    param_key_size = 0;
    param_sha3_key = NULL;

    sysparam_get_int8("board", (int8_t *)&param_board);
    sysparam_get_int8("pms_uart", (int8_t *)&param_pms_uart);
    sysparam_get_int8("i2c_scl", (int8_t *)&param_i2c_scl);
    sysparam_get_int8("i2c_sda", (int8_t *)&param_i2c_sda);

    sysparam_get_string("web_server", &param_web_server);
    sysparam_get_string("web_port", &param_web_port);
    sysparam_get_string("web_path", &param_web_path);

    sysparam_get_int32("sensor_id", (int32_t *)&param_sensor_id);
    sysparam_get_int32("key_size", (int32_t *)&param_key_size);
    size_t actual_length;
    status = sysparam_get_data("key", &param_sha3_key, &actual_length, NULL);
    if (status != SYSPARAM_OK || actual_length != param_key_size) {
        param_key_size = 0;
        param_sha3_key = NULL;
    }
}
