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
#include <ctype.h>

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
#include "bme280.h"

#include "config.h"
#include "wificfg/wificfg.h"
#include "sysparam.h"


static const char http_success_header[] = "HTTP/1.0 200 \r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "\r\n";

static const char *http_index_content[] =
{ "<!DOCTYPE html><html lang=\"en\">"
  "<head>"
  "<link rel=\"stylesheet\" type=\"text/css\" href=\"/style.css\">"
  "<script src=\"/script.js\"></script>"
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
  "</head>"
  "<body>"
  "<ul class=\"topnav\" id=\"myTopnav\">"
  "<li class=\"active\"><a href=\"/\">Home</a></li>"
  "<li><a href=\"/config.html\">Sensor Config</a></li>"
  "<li><a href=\"/wificfg/\">WiFi Config</a></li>"
  "<li class=\"icon\">"
  "<a href=\"javascript:void(0);\" onclick=\"myFunction()\">&#9776;</a>"
  "</li>"
  "</ul>",
  "</body></html>"
};

static void handle_index(int s, wificfg_method method,
                         uint32_t content_length,
                         wificfg_content_type content_type,
                         char *buf, size_t len)
{
    if (wificfg_write_string(s, http_success_header) < 0) return;
    
    if (method != HTTP_METHOD_HEAD) {
        if (wificfg_write_string(s, http_index_content[0]) < 0) return;

        if (wificfg_write_string(s, "<dl class=\"dlh\">") < 0) return;

        {
            struct tm time;
            float temp;

            if (ds3231_time_temp(&time, &temp)) {
                if (wificfg_write_string(s, "<dt>DS3231</dt>") < 0) return;
                const char *wday[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
                snprintf(buf, len, "<dd>%02u:%02u:%02u %s %u/%u/%u", time.tm_hour, time.tm_min, time.tm_sec, wday[time.tm_wday], time.tm_mday, time.tm_mon + 1, time.tm_year + 1900);
                if (wificfg_write_string(s, buf) < 0) return;
                snprintf(buf, len, ", %.1f Deg C</dd>", temp);
                if (wificfg_write_string(s, buf) < 0) return;
            }
        }

        {
            float temp, rh;
            if (sht2x_temp_rh(&temp, &rh)) {
                if (wificfg_write_string(s, "<dt>SHT2x</dt>") < 0) return;
                snprintf(buf, len, "<dd>%.1f Deg C, %.1f %% RH</dd>", temp, rh);
                if (wificfg_write_string(s, buf) < 0) return;
            }
        }

        {
            float temp, press, rh;
            if (bme280_temp_press_rh(&temp, &press, &rh)) {
                if (wificfg_write_string(s, "<dt>BME280</dt>") < 0) return;
                snprintf(buf, len, "<dd>%.1f Deg C, %.0f, %.1f %% RH</dd>", temp, press, rh);
                if (wificfg_write_string(s, buf) < 0) return;
            }
        }

        if (wificfg_write_string(s, "</dl>") < 0) return;

        if (wificfg_write_string(s, http_index_content[1]) < 0) return;
    }
}


static const char *http_config_content[] =
{ "<!DOCTYPE html><html lang=\"en\">"
  "<head>"
  "<link rel=\"stylesheet\" type=\"text/css\" href=\"/style.css\">"
  "<script src=\"/script.js\"></script>"
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
  "</head>"
  "<body>"
  "<ul class=\"topnav\" id=\"myTopnav\">"
  "<li><a href=\"/\">Home</a></li>"
  "<li class=\"active\"><a href=\"/config.html\">Sensor Config</a></li>"
  "<li><a href=\"/wificfg/\">WiFi Config</a></li>"
  "<li class=\"icon\">"
  "<a href=\"javascript:void(0);\" onclick=\"myFunction()\">&#9776;</a>"
  "</li>"
  "</ul>"
  "<form action=\"/config.html\" method=\"post\">"
  "<fieldset>"
  "<legend>Sensor configuration</legend>"
  "<dl class=\"dlh\">"
  "<dt><label for=\"board\">Board (for LEDS)</label></dt>"
  "<dd><select id=\"board\" name=\"oaq_board\">"
  "<option value=\"0\"",
  ">Nodemcu</option>"
  "<option value=\"1\"",
  ">Witty</option>"
  "</select></dd>"
  "<dt><label for=\"pms\">PMS UART</label></dt>"
  "<dd><select id=\"pms\" name=\"oaq_pms_uart\">"
  "<option value=\"0\"",
  ">None (no PMS)</option>"
  "<option value=\"1\"",
  ">RX on GPIO3, Nodemcu pin D9</option>"
  "<option value=\"3\"",
  ">RX on GPIO13, Nodemcu pin D7</option>"
  "</select></dd>"
  "<dt><label for=\"scl\">I2C SCL</label></dt>"
  "<dd><input id=\"scl\" type=\"number\" min=\"0\" max=\"15\" step=\"1\" "
  "name=\"oaq_i2c_scl\" placeholder=\"0\" value=\"",
  "\"></dd>"
  "<dt><label for=\"sda\">I2C SDA</label></dt>"
  "<dd><input id=\"sda\" type=\"number\" min=\"0\" max=\"15\" step=\"1\" "
  "name=\"oaq_i2c_sda\" placeholder=\"0\" value=\"",
  "\"></dd>"
  "<dt><label for=\"server\">Web server</label></dt>"
  "<dd><input id=\"server\" type=\"text\" maxlength=\"31\" name=\"oaq_web_server\" "
  "placeholder=\"mywebserver.org\" value=\"",
  "\"></dd>"
  "<dt><label for=\"port\">Web server port</label></dt>"
  "<dd><input id=\"port\" type=\"number\" min=\"0\" max=\"65535\" step=\"1\" "
  "name=\"oaq_web_port\" placeholder=\"80\" value=\"",
  "\"></dd>"
  "<dt><label for=\"path\">Web server path</label></dt>"
  "<dd><input id=\"path\" type=\"text\" maxlength=\"31\" name=\"oaq_web_path\" "
  "placeholder=\"/cgi-bin/recv\" value=\"",
  "\"></dd>"
  "<dt><label for=\"id\">Sensor ID</label></dt>"
  "<dd><input id=\"id\" type=\"number\" min=\"0\" max=\"4294967296\" step=\"1\" "
  "name=\"oaq_sensor_id\" placeholder=\"A 32 bit unsigned number\" value=\"",
  "\"></dd>"
  "<dt><label for=\"key\">SHA3 Key</label></dt>"
  "<dd><textarea id=\"key\" name=\"oaq_sha3_key\" maxlength=\"860\""
  "placeholder=\"A 287 byte, base64 encoded, key.\">",
  "</textarea></dd>"
  "</dl>"
  "<center><input type=\"reset\">&nbsp;<input type=\"submit\" value=\"Save\"></center>"
  "</fieldset>"
  "</form>"
  "</body></html>"
};

static char base64codes[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

static int write_base64(int s, uint8_t *in, size_t len)
{
    int i;
    for (i = 0; i < len; i += 3)  {
        char buf[4];
        buf[0] = base64codes[(in[i] & 0xFC) >> 2];
        uint8_t b = (in[i] & 0x03) << 4;
        if (i + 1 < len) {
            b |= (in[i + 1] & 0xF0) >> 4;
            buf[1] = base64codes[b];
            b = (in[i + 1] & 0x0F) << 2;
            if (i + 2 < len)  {
                b |= (in[i + 2] & 0xC0) >> 6;
                buf[2] = base64codes[b];
                b = in[i + 2] & 0x3F;
                buf[3] = base64codes[b];
            } else  {
                buf[2] = base64codes[b];
                buf[3] = '=';
            }
        } else      {
            buf[1] = base64codes[b];
            buf[2] = '=';
            buf[3] = '=';
        }
        int count = write(s, buf, 4);
        if (count < 4)
            return count;
    }
    return len;
}

static void handle_config(int s, wificfg_method method,
                           uint32_t content_length,
                           wificfg_content_type content_type,
                           char *buf, size_t len)
{
    if (wificfg_write_string(s, http_success_header) < 0) return;

    if (method != HTTP_METHOD_HEAD) {
        if (wificfg_write_string(s, http_config_content[0]) < 0) return;

        int8_t board = 0; /* Nodemcu */
        sysparam_get_int8("oaq_board", &board);
        if (board == 0 && wificfg_write_string(s, " selected") < 0) return;
        if (wificfg_write_string(s, http_config_content[1]) < 0) return;
        if (board == 1 && wificfg_write_string(s, " selected") < 0) return;
        if (wificfg_write_string(s, http_config_content[2]) < 0) return;

        int8_t pms_uart = 1; /* Enabled, RX */
        sysparam_get_int8("oaq_pms_uart", &pms_uart);
        if (pms_uart == 0 && wificfg_write_string(s, " selected") < 0) return;
        if (wificfg_write_string(s, http_config_content[3]) < 0) return;
        if (pms_uart == 1 && wificfg_write_string(s, " selected") < 0) return;
        if (wificfg_write_string(s, http_config_content[4]) < 0) return;
        if (pms_uart == 2 && wificfg_write_string(s, " selected") < 0) return;
        if (wificfg_write_string(s, http_config_content[5]) < 0) return;

        int8_t i2c_scl = 0;
        sysparam_get_int8("oaq_i2c_scl", &i2c_scl);
        snprintf(buf, len, "%d", i2c_scl);
        if (wificfg_write_string(s, buf) < 0) return;

        if (wificfg_write_string(s, http_config_content[6]) < 0) return;

        int8_t i2c_sda = 2;
        sysparam_get_int8("oaq_i2c_sda", &i2c_sda);
        snprintf(buf, len, "%d", i2c_sda);
        if (wificfg_write_string(s, buf) < 0) return;

        if (wificfg_write_string(s, http_config_content[7]) < 0) return;

        char *web_server = NULL;
        sysparam_get_string("oaq_web_server", &web_server);
        if (web_server) {
            wificfg_html_escape(web_server, buf, len);
            free(web_server);
            if (wificfg_write_string(s, buf) < 0) return;
        }

        if (wificfg_write_string(s, http_config_content[8]) < 0) return;

        int32_t web_port = 80;
        sysparam_get_int32("oaq_web_port", &web_port);
        snprintf(buf, len, "%d", web_port);
        if (wificfg_write_string(s, buf) < 0) return;

        if (wificfg_write_string(s, http_config_content[9]) < 0) return;

        char *web_path = NULL;
        sysparam_get_string("oaq_web_path", &web_path);
        if (web_path) {
            wificfg_html_escape(web_path, buf, len);
            free(web_path);
        } else {
            wificfg_html_escape("/cgi-bin/recv", buf, len);
        }
        if (wificfg_write_string(s, buf) < 0) return;

        if (wificfg_write_string(s, http_config_content[10]) < 0) return;

        int32_t sensor_id = 0;
        if (sysparam_get_int32("oaq_sensor_id", &sensor_id) == SYSPARAM_OK) {
            snprintf(buf, len, "%u", sensor_id);
            if (wificfg_write_string(s, buf) < 0) return;
        }

        if (wificfg_write_string(s, http_config_content[11]) < 0) return;

        uint8_t *sha3_key = NULL;
        size_t actual_length;
        if (sysparam_get_data("oaq_sha3_key", &sha3_key, &actual_length, NULL) == SYSPARAM_OK) {
            if (sha3_key) {
                if (write_base64(s, sha3_key, actual_length) < actual_length) return;
                free(sha3_key);
            }
        }

        if (wificfg_write_string(s, http_config_content[12]) < 0) return;
    }
}

typedef enum {
    FORM_NAME_BOARD,
    FORM_NAME_PMS_UART,
    FORM_NAME_I2C_SCL,
    FORM_NAME_I2C_SDA,
    FORM_NAME_WEB_SERVER,
    FORM_NAME_WEB_PORT,
    FORM_NAME_WEB_PATH,
    FORM_NAME_SENSOR_ID,
    FORM_NAME_SHA3_KEY,
    FORM_NAME_NONE
} form_name;

static const struct {
    const char *str;
    form_name name;
} form_name_table[] = {
    {"oaq_board", FORM_NAME_BOARD},
    {"oaq_pms_uart", FORM_NAME_PMS_UART},
    {"oaq_i2c_scl", FORM_NAME_I2C_SCL},
    {"oaq_i2c_sda", FORM_NAME_I2C_SDA},
    {"oaq_web_server", FORM_NAME_WEB_SERVER},
    {"oaq_web_port", FORM_NAME_WEB_PORT},
    {"oaq_web_path", FORM_NAME_WEB_PATH},
    {"oaq_sensor_id", FORM_NAME_SENSOR_ID},
    {"oaq_sha3_key", FORM_NAME_SHA3_KEY},
};

static form_name intern_form_name(char *str)
{
     int i;
     for (i = 0;  i < sizeof(form_name_table) / sizeof(form_name_table[0]); i++) {
         if (!strcmp(str, form_name_table[i].str))
             return form_name_table[i].name;
     }
     return FORM_NAME_NONE;
}

static const char http_redirect_header[] = "HTTP/1.0 302 \r\n"
    "Location: /\r\n"
    "\r\n";


static const uint8_t base64table[256] =
{
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 62, 65, 65, 65, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 65, 65, 65, 64, 65, 65,
    65,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 65, 65, 65, 65, 65,
    65, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65
};

/*
 * Decode a form-url encoded value with base64 encoded binary data.
 */
static uint8_t *read_base64(int s, int len, size_t *rem)
{
    uint8_t *decoded = malloc(len);
    if (!decoded)
        return NULL;

    int j = 0;
    while (*rem > 0 && j < len) {
        /* Read four characters ignoring noise */
        uint8_t b[4];
        int n = 0;
        while (rem > 0) {
            char c;
            int r = read(s, &c, 1);
            /* Expecting a known number of characters so fail on EOF. */
            if (r < 1) return NULL;
            (*rem)--;
            if (c == '%') {
                if (*rem < 2)
                    return NULL;
                unsigned char c2[2];
                int r = read(s, &c2, 2);
                if (r < 0) return NULL;
                (*rem) -= r;
                if (r < 2) return NULL;
                if (isxdigit(c2[0]) && isxdigit(c2[1])) {
                    c2[0] = tolower(c2[0]);
                    int d1 = (c2[0] >= 'a' && c2[0] <= 'z') ? c2[0] - 'a' + 10 : c2[0] - '0';
                    c2[1] = tolower(c2[1]);
                    int d2 = (c2[1] >= 'a' && c2[1] <= 'z') ? c2[1] - 'a' + 10 : c2[1] - '0';
                    int v = base64table[(d1 << 4) + d2];
                    if (v < 65) {
                        b[n++] = v;
                        if (n >= 4)
                            break;
                    }
                }
            } else if (c == '&') {
                /* Ended early. */
                return NULL;
            } else {
                int v = base64table[(int)c];
                if (v < 65) {
                    b[n++] = v;
                    if (n >= 4)
                        break;
                }
            }
        }

        decoded[j++] = (b[0] << 2) | (b[1] >> 4);
        if (j >= len)
            return decoded;
        if (b[2] < 64) {
            decoded[j++] = (b[1] << 4) | (b[2] >> 2);
            if (j >= len)
                return decoded;
            if (b[3] < 64) {
                decoded[j++] = (b[2] << 6) | b[3];
            }
        }
    }

    return decoded;
}

static void handle_config_post(int s, wificfg_method method,
                                uint32_t content_length,
                                wificfg_content_type content_type,
                                char *buf, size_t len)
{
    if (content_type != HTTP_CONTENT_TYPE_WWW_FORM_URLENCODED) {
        wificfg_write_string(s, "HTTP/1.0 400 \r\nContent-Type: text/html\r\n\r\n");
        return;
    }

    size_t rem = content_length;
    bool valp = false;

    while (rem > 0) {
        int r = wificfg_form_name_value(s, &valp, &rem, buf, len);

        if (r < 0) {
            break;
        }

        wificfg_form_url_decode(buf);

        form_name name = intern_form_name(buf);

        if (valp) {
            if (name == FORM_NAME_SHA3_KEY) {
                uint8_t *key = read_base64(s, 287, &rem);
                if (key) {
                    sysparam_set_data("oaq_sha3_key", key, 287, true);
                    free(key);
                }
            } else {
                int r = wificfg_form_name_value(s, NULL, &rem, buf, len);
                if (r < 0) {
                    break;
                }

                wificfg_form_url_decode(buf);

                switch (name) {
                case FORM_NAME_BOARD: {
                    int8_t board = strtol(buf, NULL, 10);
                    if (board >= 0 && board <= 1)
                        sysparam_set_int8("oaq_board", board);
                    break;
                }
                case FORM_NAME_PMS_UART: {
                    int8_t uart = strtol(buf, NULL, 10);
                    if (uart >= 0 && uart <= 2)
                        sysparam_set_int8("oaq_pms_uart", uart);
                    break;
                }
                case FORM_NAME_I2C_SCL: {
                    int8_t i2c_scl = strtol(buf, NULL, 10);
                    if (i2c_scl >= 0 && i2c_scl <= 15)
                        sysparam_set_int8("oaq_i2c_scl", i2c_scl);
                    break;
                }
                case FORM_NAME_I2C_SDA: {
                    int8_t i2c_sda = strtol(buf, NULL, 10);
                    if (i2c_sda >= 0 && i2c_sda <= 15)
                        sysparam_set_int8("oaq_i2c_sda", i2c_sda);
                    break;
                }
                case FORM_NAME_WEB_SERVER: {
                    sysparam_set_string("oaq_web_server", buf);
                    break;
                }
                case FORM_NAME_WEB_PORT: {
                    int32_t port = strtol(buf, NULL, 10);
                    if (port >= 0 && port <= 65535)
                        sysparam_set_int32("oaq_web_port", port);
                    break;
                }
                case FORM_NAME_WEB_PATH: {
                    sysparam_set_string("oaq_web_path", buf);
                    break;
                }
                case FORM_NAME_SENSOR_ID: {
                    int32_t id = strtol(buf, NULL, 10);
                    sysparam_set_int32("oaq_sensor_id", id);
                    break;
                }
                case FORM_NAME_SHA3_KEY:
                default:
                    break;
                }
            }
        }
    }

    wificfg_write_string(s, http_redirect_header);
}


static const wificfg_dispatch dispatch_list[] = {
    {"/", HTTP_METHOD_GET, handle_index},
    {"/index.html", HTTP_METHOD_GET, handle_index},
    {"/config", HTTP_METHOD_GET, handle_config},
    {"/config.html", HTTP_METHOD_GET, handle_config},
    {"/config", HTTP_METHOD_POST, handle_config_post},
    {"/config.html", HTTP_METHOD_POST, handle_config_post},
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
