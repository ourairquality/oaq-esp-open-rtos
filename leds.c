/*
 * LED support for blinking progress.
 *
 * Copyright (C) 2016, 2017 OurAirQuality.org
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

#include "FreeRTOS.h"
#include "task.h"
#include "esp/gpio.h"
#include "config.h"

void init_blink()
{
    switch (param_board) {
      case 0:
          // Nodemcu
          gpio_enable(16, GPIO_OUTPUT);
          gpio_write(16, 1);
          break;

      case 1:
          /*
           * Witty param_board setup.
           * Multi-color LED is on pins 12 13 and 15.
           */

          //iomux_set_gpio_function(12, 1);
          //iomux_set_gpio_function(13, 1);
          //iomux_set_gpio_function(15, 1);
          gpio_enable(12, GPIO_OUTPUT); // Green
          gpio_enable(13, GPIO_OUTPUT); // Blue
          gpio_enable(15, GPIO_OUTPUT); // Red
          gpio_write(12, 0);
          gpio_write(13, 0);
          gpio_write(15, 0);
          break;
    }
}

void blink_green()
{
    switch (param_board) {
      case 0:
          // Nodemcu
          gpio_write(16, 0);
          taskYIELD();
          gpio_write(16, 1);
          break;
      case 1 :
          // Witty Green
          gpio_write(12, 1);
          taskYIELD();
          gpio_write(12, 0);
          break;
    }
}

void blink_blue()
{
    switch (param_board) {
      case 0:
          // Nodemcu
          gpio_write(16, 0);
          taskYIELD();
          gpio_write(16, 1);
          break;
      case 1:
          // Witty Blue
          gpio_write(13, 1);
          taskYIELD();
          gpio_write(13, 0);
          break;
    }
}

void blink_red()
{
    switch (param_board) {
      case 0:
          // Nodemcu
          gpio_write(16, 0);
          taskYIELD();
          gpio_write(16, 1);
          break;

      case 1:
          // Witty Red
          gpio_write(15, 1);
          taskYIELD();
          gpio_write(15, 0);
          break;
    }
}

void blink_white()
{
    switch (param_board) {
      case 0:
          // Nodemcu.
          gpio_write(16, 0);
          taskYIELD();
          gpio_write(16, 1);
          break;

      case 1:
          // Witty Green, Blue, Red
          gpio_write(12, 1);
          gpio_write(13, 1);
          gpio_write(15, 1);
          taskYIELD();
          gpio_write(12, 0);
          gpio_write(13, 0);
          gpio_write(15, 0);
    }
}
