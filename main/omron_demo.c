/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/****************************************************************************
 *
 * This demo showcases BLE GATT client. It can scan BLE devices and connect to one device.
 * Run the gatt_server demo, the client demo will automatically connect to the gatt_server demo.
 * Client demo will enable gatt_server's notify after connection. The two devices will then exchange
 * data.
 *
 ****************************************************************************/

// Include C standard libraries
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// Include ESP-IDF libraries
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatt_defs.h>
#include <esp_gattc_api.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <nvs.h>
#include <nvs_flash.h>

// Include Notecard note-c library
#include <note.h>

// Notecard node-c helper methods
#include "note_c_hooks.h"

// Include OMRON BLE files
#include "omron_defs.h"
#include "omron_gap_scan.h"
#include "omron_gatt_scan.h"

// Uncomment the define below and replace com.your-company:your-product-name
// with your ProductUID.
#define PRODUCT_UID "com.zakoverflow.test"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

#define LOG_TAG "OMRON_DEMO"

#define NOTECARD_REQUEST_TIMEOUT_S 5

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ONLY_WLST,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
};

/* Declare static functions */
static esp_err_t initBluetoothController(void)
{
    esp_err_t ret;

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_BLE;
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return ret;
    }

    // ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    ret = esp_bt_controller_enable(bt_cfg.mode);
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return ret;
    }

    return ret;
}

static esp_err_t initBluetoothHost(void)
{
    esp_err_t ret;

    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return ret;
    }

    return ret;
}

void app_main(void)
{
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialize note-c hooks
    NoteSetUserAgent((char *)"note-esp32");
    NoteSetFn(malloc, free, platform_delay, platform_millis);
    NoteSetFnDebugOutput(note_log_print);
    NoteSetFnI2C(NOTE_I2C_ADDR_DEFAULT, NOTE_I2C_MAX_DEFAULT, note_i2c_reset, note_i2c_transmit, note_i2c_receive);

    // Send a Notecard hub.set using note-c
    J *req;
    if ((req = NoteNewRequest("hub.set")))
    {
        JAddStringToObject(req, "product", PRODUCT_UID);
        JAddStringToObject(req, "mode", "periodic");
        JAddStringToObject(req, "sn", "omron-relay");
        if (!NoteRequestWithRetry(req, NOTECARD_REQUEST_TIMEOUT_S))
        {
            NoteDebug("Failed to configure Notecard.");
            return;
        }
    }
    else
    {
        NoteDebug("Failed to allocate memory.");
        return;
    }

    // Initialize the Bluetooth Controller
    err = initBluetoothController();
    ESP_ERROR_CHECK(err);

    // Initialize the Bluetooth Host
    err = initBluetoothHost();
    ESP_ERROR_CHECK(err);

    // Register the callback function to the gap module
    err = esp_ble_gap_register_callback(esp_gap_cb);
    if (err)
    {
        ESP_LOGE(LOG_TAG, "%s gap register failed, error code = 0x%x\n", __func__, err);
        return;
    }

    // Register the callback function to the gattc module
    err = esp_ble_gattc_register_callback(esp_gattc_intercept_cb);
    if (err)
    {
        ESP_LOGE(LOG_TAG, "%s gattc register failed, error code = 0x%x\n", __func__, err);
        return;
    }

    err = esp_ble_gattc_app_register(PROFILE_OMRON_ID);
    if (err)
    {
        ESP_LOGE(LOG_TAG, "%s gattc app register failed, error code = 0x%x\n", __func__, err);
    }

    // Setting GAP parameters will begin scanning for BLE devices
    err = esp_ble_gap_set_scan_params(&ble_scan_params);
    if (err)
    {
        ESP_LOGE(LOG_TAG, "set scan params error, error code = 0x%x", err);
    }
}
