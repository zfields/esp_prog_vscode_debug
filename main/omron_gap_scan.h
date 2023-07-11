#ifndef OMRON_GAP_SCAN_H
#define OMRON_GAP_SCAN_H

// Include ESP-IDF libraries
#include <esp_gap_ble_api.h>

#define REMOTE_BLE_4_0_MTU_SIZE 23

void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

#endif // OMRON_GAP_SCAN_H
