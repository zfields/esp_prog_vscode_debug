#include "note_c_hooks.h"

// Copyright 2020 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <string.h>
#include <time.h>
#include <sys/cdefs.h>
#include <sys/time.h>

// Board-level configs
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#define ESP32_I2C_PORT          I2C_NUM_0
#define ESP32_I2C_FREQUENCY     100000
#define ESP32_I2C_SDA_PIN       23
#define ESP32_I2C_SCL_PIN       22
#define ESP32_I2C_RX_BUF        0
#define ESP32_I2C_TX_BUF        0
#define ESP32_INTR_FLAGS        0
#define ESP32_I2C_TIMEOUT_MS    (TickType_t)5000

void platform_delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t platform_millis(void) {
    // return (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
    return (uint32_t) (time_us / 1000L);
}

const char *note_i2c_receive(uint16_t device_address_, uint8_t *buffer_, uint16_t size_, uint32_t *available_) {
    const char *errstr;

    // Retry transmit errors several times, because it's harmless to do so
    errstr = "i2c: write error {io}";
    for (int i=0; i<3; i++) {
        uint8_t hdr[2];
        hdr[0] = (uint8_t) 0;
        hdr[1] = (uint8_t) size_;
        esp_err_t err = i2c_master_write_to_device(ESP32_I2C_PORT, device_address_, hdr, sizeof(hdr), pdMS_TO_TICKS(ESP32_I2C_TIMEOUT_MS));
        if (ESP_OK == err) {
            errstr = NULL;
            break;
        } else {
            ESP_LOGE("NOTE-C", "%s", esp_err_to_name(err));
        }
    }
    if (errstr != NULL) {
        return errstr;
    }

    // Only receive if we successfully began transmission
    int readlen = size_ + (sizeof(uint8_t)*2);
    uint8_t *readbuf = (uint8_t *)malloc(readlen);
    if (!readbuf) {
        return "i2c: insufficient memory";
    }
    esp_err_t err = i2c_master_read_from_device(ESP32_I2C_PORT, device_address_, readbuf, readlen, pdMS_TO_TICKS(ESP32_I2C_TIMEOUT_MS));
    if (ESP_OK != err) {
        free(readbuf);
        ESP_LOGE("NOTE-C", "%s", esp_err_to_name(err));
        return "i2c: receive error {io}";
    }
    uint8_t availbyte = readbuf[0];
    uint8_t goodbyte = readbuf[1];
    if (goodbyte != size_) {
        free(readbuf);
        return "i2c: incorrect amount of data";
    }

    // Done
    *available_ = availbyte;
    memcpy(buffer_, &readbuf[2], size_);
    free(readbuf);
    return NULL;
}

bool note_i2c_reset(uint16_t device_address_) {
    // Release the device
    i2c_driver_delete(ESP32_I2C_PORT);

    // Re-enable the device
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .sda_io_num = ESP32_I2C_SDA_PIN,
        .scl_io_num = ESP32_I2C_SCL_PIN,
        .master.clk_speed = ESP32_I2C_FREQUENCY,
    };
    esp_err_t err;
    if ((err = i2c_param_config(ESP32_I2C_PORT, &conf))) {
        ESP_LOGE("NOTE-C", "%s", esp_err_to_name(err));
    } else if ((err = i2c_driver_install(ESP32_I2C_PORT, conf.mode, ESP32_I2C_RX_BUF, ESP32_I2C_TX_BUF, ESP32_INTR_FLAGS))) {
        ESP_LOGE("NOTE-C", "%s", esp_err_to_name(err));
    } else if ((err = i2c_set_timeout(ESP32_I2C_PORT, 1048575))) {
        ESP_LOGE("NOTE-C", "%s", esp_err_to_name(err));
    }

    return (ESP_OK == err);
}

const char *note_i2c_transmit(uint16_t device_address_, uint8_t *buffer_, uint16_t size_) {
    int writelen = sizeof(uint8_t) + size_;
    uint8_t *writebuf = (uint8_t *)malloc(writelen);
    if (!writebuf) {
        return "i2c: insufficient memory";
    }
    writebuf[0] = size_;
    memcpy(&writebuf[1], buffer_, size_);
    esp_err_t err = i2c_master_write_to_device(ESP32_I2C_PORT, device_address_, writebuf, writelen, pdMS_TO_TICKS(ESP32_I2C_TIMEOUT_MS));
    free(writebuf);
    if (ESP_OK != err) {
        ESP_LOGE("NOTE-C", "%s", esp_err_to_name(err));
        return "i2c: write error {io}";
    }
    return NULL;
}

size_t note_log_print(const char *message_) {
    ESP_LOGI("NOTE-C", "%s", message_);
    return strnlen(message_, 255);
}
