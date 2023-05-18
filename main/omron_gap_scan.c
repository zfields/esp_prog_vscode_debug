#include "omron_gap_scan.h"

#include <string.h>

#include <esp_bt_defs.h>
#include <esp_log.h>

#include "omron_gatt_dis.h"
#include "omron_profile_id.h"

#define LOG_TAG "GAP_SCAN"

volatile bool connect = false;
static const char remote_device_prefix[] = "BLEsmart_00000154";
static esp_bd_addr_t omron_blm_device_addr = {0x00,0x5F,0xBF,0x9F,0x9C,0x11};

void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    switch (event)
    {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    {
        // the unit of the duration is second
        uint32_t duration_s = 30;
        esp_ble_gap_update_whitelist(ESP_BLE_WHITELIST_ADD, omron_blm_device_addr, BLE_ADDR_TYPE_PUBLIC);
        esp_ble_gap_start_scanning(duration_s);
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        // scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(LOG_TAG, "scan start failed, error status = 0x%x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "scan start success");

        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt)
        {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            esp_log_buffer_hex(LOG_TAG, scan_result->scan_rst.bda, 6);
            ESP_LOGI(LOG_TAG, "searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            ESP_LOGI(LOG_TAG, "searched Device Name Len %d", adv_name_len);
            esp_log_buffer_char(LOG_TAG, adv_name, adv_name_len);

#if CONFIG_EXAMPLE_DUMP_ADV_DATA_AND_SCAN_RESP
            if (scan_result->scan_rst.adv_data_len > 0)
            {
                ESP_LOGI(LOG_TAG, "adv data:");
                esp_log_buffer_hex(LOG_TAG, &scan_result->scan_rst.ble_adv[0], scan_result->scan_rst.adv_data_len);
            }
            if (scan_result->scan_rst.scan_rsp_len > 0)
            {
                ESP_LOGI(LOG_TAG, "scan resp:");
                esp_log_buffer_hex(LOG_TAG, &scan_result->scan_rst.ble_adv[scan_result->scan_rst.adv_data_len], scan_result->scan_rst.scan_rsp_len);
            }
#endif
            ESP_LOGI(LOG_TAG, "\n");

            if (adv_name != NULL)
            {
                if ((char *)adv_name == strstr((char *)adv_name, remote_device_prefix))
                {
                    ESP_LOGI(LOG_TAG, "discovered %s device (%s)\n", remote_device_prefix, (char *)adv_name);
                    if (connect == false)
                    {
                        connect = true;
                        ESP_LOGI(LOG_TAG, "connecting to the remote device...");
                        esp_ble_gap_stop_scanning();
                        esp_ble_gattc_open(gl_profile_tab[PROFILE_DIS_ID].gattc_if, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type, true);
                    }
                }
            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(LOG_TAG, "scan stop failed, error status = 0x%x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "stop scan successfully");
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(LOG_TAG, "adv stop failed, error status = 0x%x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "stop adv successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(LOG_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                 param->update_conn_params.status,
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}
