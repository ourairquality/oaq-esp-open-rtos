# Our Air Quality logger for the ESP8266 running the [ESP Open RTOS](https://github.com/SuperHouse/esp-open-rtos)

A community developed open source air quality data logger.

Currently supporting the [Plantower](http://plantower/) PMS1003, PMS3003, PMS5003, and PMS7003 particle counters.

Supports the SHT2x temperature and relative humidity sensors. This data might be important for qualifying if the air conditions support reliable PM measurement.

Supports the BMP180 air pressure sensor on the same I2C bus as the SHT2x and auto detected.


## Building

Follow the build instructions for esp-open-rtos, and when the examples are running copy this code into `examples/oaq/`.

This code supports a 4MiB SPI flash, so the ESP12, and the Nodemcu and Witty boards have been tested. For this flash it was necessary to edit `parameters.mk` to change `FLASH_SIZE ?= 32` and `FLASH_MODE ?= dio`.

The following will build the code and flash a device on Linux.

`make flash -j4 -C examples/oaq ESPPORT=/dev/ttyUSB0`


## Features

* Logs the data to a memory buffer and then to the SPI flash storage, using 3MiB of the 4MiB for data storage. This allows the data logger to be used out of reach of Wifi and the Internet for a period.

* All the data from the Plantower sensor is logged, each sample at 0.8 second intervals, and all the data in the samples which includes the PM1.0, PM2.5, and PM10 values plus the particle counts, and even the checksum for each sample. This might be useful for local sources of pollution that can cause quick changes in air quality, and might be useful for post-analysis such as noise reduction.

* The data is compressed to fit more data into the flash and this also reduces wear on the flash and perhaps power usage. The particle count distributions are converted to differential values reducing their magnitude, and after delta encoding they are encoding using a custom variable bit length code. There are special events for the case of no change in the values in which case only a time delta is encoded. This typically compresses the data to 33% to 15% of the original size.

* The compressed data is stored in flash sectors, and each sector stands on its own and can be uncompressed on its own. An attempt is made to handle bad sectors, in which case the data is written to the next good sector. Each valid sector is assigned a monotonically increasing 32-bit index. The sectors are organized as a ring-buffer, so when full the oldest is overwritten. The sectors are buffered in memory before writing to reduce the number of writes and the current data is periodically flushed to the flash storage to avoid too much data loss if power is lost. ESP flash tools can read these sectors for downloading the data without Wifi.

* The compressed sectors are HTTP-POSTed to a server. The current head sector is periodically posted to the server too to keep it updated and only the new data is posted. The server response can request re-sending of sectors still stored on the device to handle data loss at the server. The server can not affected the data stored on the device or the logging of the data to flash as a safety measure.

* The ESP8266 Real-Time-Clock (RTC) counter is logged with every event. The server response includes the real time and response events are logged allowing estimation of the real time of events in post-analysis. Support for logging a button press will be added to allow people to synchronize logging times manually.

* The data posted to the server is signed using the MAC-SHA3 algorithm ensuring integrity of the data and preventing forgery of data posted to the server.


## TODO

This is at the proof of concept stage.

The URL to upload the data to and the key is hard coded, see `post.c`, and this needs to be configurable.

The protocol for storing the data and uploading will no doubt need a lot of revision.

The server side code is just some hack code at this stage, it can recover the data and verify the checksums. The plan is to write CGI code in C to receive the data and validate the signature and store it on the server, and to provide an API for access to the data, which is expected to work with common and economical cPanel shared hosting.
