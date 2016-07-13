/*
 * LED support for blinking progress.
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

void init_blink()
{
    /*
     * Witty board setup.
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
}

void blink_green()
{
    // Witty Green
    gpio_write(12, 1);
    taskYIELD();
    gpio_write(12, 0);
}

void blink_blue()
{
    // Witty Blue
    gpio_write(13, 1);
    taskYIELD();
    gpio_write(13, 0);
}

void blink_red()
{
    // Witty Red
    gpio_write(15, 1);
    taskYIELD();
    gpio_write(15, 0);
}

void blink_white()
{
    // Witty Red
    gpio_write(12, 1);
    gpio_write(13, 1);
    gpio_write(15, 1);
    taskYIELD();
    gpio_write(12, 0);
    gpio_write(13, 0);
    gpio_write(15, 0);
}
