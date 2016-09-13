/*
 * Web interface.
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
#include "espressif/esp_common.h"

#include <string.h>
#include <time.h>

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
#include "config.h"
#include "flash.h"
#include "sha3.h"
#include "ds3231.h"
#include "sht21.h"

#include "config.h"
#include "wificfg/wificfg.h"
#include "sysparam.h"


static const char http_success_header[] = "HTTP/1.0 200 \r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "\r\n";
static const char http_index_content1[] = "<!DOCTYPE html><html>"
    "<head>"
    "<link rel=\"icon\" href=\"data:,\">"
    "</head>" // TODO style
    "<body>";
static const char http_index_content2[] = "<a href=\"/wificfg/\">"
    "<b>Configure WiFi</b></a><br/>";
static const char http_index_content3[] = "</body></html>";

static void handle_index(int s, wificfg_method method,
                         uint32_t content_length,
                         wificfg_content_type content_type,
                         char *buf, size_t len)
{
    if (wificfg_write_string(s, http_success_header) < 0) return;
    
    if (method != HTTP_METHOD_HEAD) {
        if (wificfg_write_string(s, http_index_content1) < 0) return;

        socklen_t addr_len;
        struct sockaddr addr;
        addr_len = sizeof(addr);
        getpeername(s, (struct sockaddr*)&addr, &addr_len);

        if (addr.sa_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)&addr;
            snprintf(buf, len, "<li>Your address is: %d.%d.%d.%d",
                     ip4_addr1(&sa->sin_addr), ip4_addr2(&sa->sin_addr),
                     ip4_addr3(&sa->sin_addr), ip4_addr4(&sa->sin_addr));
            if (wificfg_write_string(s, buf) < 0) return;
            snprintf(buf, len, " port %d</li></ul>", ntohs(sa->sin_port));
            if (wificfg_write_string(s, buf) < 0) return;
        }

        {
            float temp, rh;
            if (sht2x_temp_rh(&temp, &rh)) {
                snprintf(buf, len, "SHT2x: %.1f deg C, %.1f %% RH<br>", temp, rh);
                if (wificfg_write_string(s, buf) < 0) return;
            }
        }

        if (wificfg_write_string(s, http_index_content2) < 0) return;
        if (wificfg_write_string(s, http_index_content3) < 0) return;
    }
}

static const wificfg_dispatch dispatch_list[] = {
    {"/", HTTP_METHOD_GET, handle_index},
    {"/index.html", HTTP_METHOD_GET, handle_index},
    {NULL, HTTP_METHOD_ANY, NULL}
};

void init_web()
{
    /* Default AP params. */
    char *wifi_ap_ssid = NULL;
    sysparam_get_string("wifi_ap_ssid", &wifi_ap_ssid);
    if (!wifi_ap_ssid) {
        sysparam_set_string("wifi_ap_ssid", "esp-open-rtos AP");
    } else {
        free(wifi_ap_ssid);
    }
    char *wifi_ap_password = NULL;
    sysparam_get_string("wifi_ap_password", &wifi_ap_password);
    if (!wifi_ap_password) {
        sysparam_set_string("wifi_ap_password", "esp-open-rtos");
    } else {
        free(wifi_ap_password);
    }

    sdk_wifi_set_sleep_type(WIFI_SLEEP_MODEM);

    /*
     * Always start the Wifi, even if not posting data to a server, to
     * allow local access and configuration.
     */

    wificfg_init(80, dispatch_list);
}
