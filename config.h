/*
 * Configuration parameters.
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

/*
 * Parameters.
 *
 * If not sufficiently initialized to communicate with a server then
 * wifi is disabled and the post-data task is not created.
 *
 * The 'board' can be 0 for Nodemcu, and 1 for Witty.
 */
extern char *param_web_server;
extern char *param_web_port;
extern char *param_web_path;
extern uint32_t param_sensor_id;
extern uint32_t param_key_size;
extern uint8_t *param_sha3_key;
extern uint8_t param_board;
extern uint8_t param_pms5003_serial;
extern char *param_wifi_ssid;
extern char *param_wifi_pass;

void init_params();


