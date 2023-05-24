#include "omron_gap_scan.h"

// Include C standard libraries
#include <string.h>

// Include ESP-IDF libraries
#include <esp_bt_defs.h>
#include <esp_bt_main.h>
#include <esp_log.h>
// #include <nvs.h>
// #include <nvs_flash.h>

// Include OMRON BLE files
#include "omron_defs.h"
#include "omron_gatt_scan.h"

#define LOG_TAG "GAP_SCAN"

volatile bool connect = false;
static const char remote_device_prefix[] = "BLEsmart_00000154";

void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    ESP_LOGI(LOG_TAG, "");
    switch (event)
    {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    {
        if (ESP_BT_STATUS_SUCCESS != param->scan_param_cmpl.status)
        {
            ESP_LOGE(LOG_TAG, "SCAN_PARAM_SET_COMPLETE_EVT [%d]", event);
            ESP_LOGE(LOG_TAG, "[%d]\tStatus: %d", event, param->scan_param_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%d]", event);
            break;
        }

        ESP_LOGI(LOG_TAG, "SCAN_PARAM_SET_COMPLETE_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tStatus: %d", event, param->scan_param_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        // Report the result of the scan parameter update
        ESP_LOGI(LOG_TAG, "[%d] scan parameters set", event);

        // Add known device to the whitelist
        esp_ble_gap_update_whitelist(ESP_BLE_WHITELIST_ADD, OMRON_DEVICE_ADDR, BLE_ADDR_TYPE_PUBLIC);
        ESP_LOGI(LOG_TAG, "[%d] adding device (%02X:%02X:%02X:%02X:%02X:%02X) to whitelist...", event, OMRON_DEVICE_ADDR[0], OMRON_DEVICE_ADDR[1], OMRON_DEVICE_ADDR[2], OMRON_DEVICE_ADDR[3], OMRON_DEVICE_ADDR[4], OMRON_DEVICE_ADDR[5]);
        break;
    }
    case ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT:
        if (ESP_BT_STATUS_SUCCESS != param->update_whitelist_cmpl.status)
        {
            ESP_LOGE(LOG_TAG, "UPDATE_WHITELIST_COMPLETE_EVT [%d]", event);
            ESP_LOGE(LOG_TAG, "[%d]\tStatus: %d", event, param->update_whitelist_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%d]", event);
            break;
        }

        ESP_LOGI(LOG_TAG, "UPDATE_WHITELIST_COMPLETE_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tStatus: %d", event, param->update_whitelist_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%d]\tOperation: %d", event, param->update_whitelist_cmpl.wl_operation);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        // Report the result of the whitelist update
        ESP_LOGI(LOG_TAG, "[%d] %s device to whitelist", event, (param->update_whitelist_cmpl.wl_operation == ESP_BLE_WHITELIST_ADD ? "added" : "removed"));

        // set the security iocap & auth_req & key size & init key response key
        // parameters to the stack
        esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND; // bonding with peer device after authentication
        esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE; // set the IO capability to No output No input
        uint8_t key_size = 16; // the key size should be 7~16 bytes
        uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
        uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
        uint8_t oob_support = ESP_BLE_OOB_DISABLE;
        esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
        esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
        esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
        esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob_support, sizeof(uint8_t));

        // If your BLE device act as a Slave, the init_key means you hope which
        // types of key of the master should distribute to you, and the response
        // key means which key you can distribute to the Master; If your BLE
        // device act as a master, the response key means you hope which types
        // of key of the slave should distribute to you, and the init key means
        // which key you can distribute to the slave.
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

        esp_ble_gap_config_local_privacy(true);
        break;
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
        if (ESP_BT_STATUS_SUCCESS != param->local_privacy_cmpl.status)
        {
            ESP_LOGE(LOG_TAG, "SET_LOCAL_PRIVACY_COMPLETE_EVT [%d]", event);
            ESP_LOGE(LOG_TAG, "[%d]\tStatus: %d", event, param->local_privacy_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%d]", event);
            break;
        }

        ESP_LOGI(LOG_TAG, "SET_LOCAL_PRIVACY_COMPLETE_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tStatus: %d", event, param->local_privacy_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        // Start scanning for BLE devices
        const uint32_t duration_s = 30;
        ESP_LOGI(LOG_TAG, "[%d] begin scanning for BLE devices for %lu seconds...", event, duration_s);
        esp_ble_gap_start_scanning(duration_s);
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (ESP_BT_STATUS_SUCCESS != param->scan_start_cmpl.status) {
            ESP_LOGE(LOG_TAG, "SCAN_START_COMPLETE_EVT [%d]", event);
            ESP_LOGE(LOG_TAG, "[%d]\tStatus: %d", event, param->scan_start_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%d]", event);
            break;
        }

        ESP_LOGI(LOG_TAG, "SCAN_START_COMPLETE_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tStatus: %d", event, param->scan_start_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        // Report the result of the scan start
        ESP_LOGI(LOG_TAG, "[%d] now scanning for BLE devices...", event);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        ESP_LOGI(LOG_TAG, "SCAN_RESULT_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        switch (param->scan_rst.search_evt)
        {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            ESP_LOGI(LOG_TAG, "[%d] SEARCH_INQ_RES_EVT [%d]", event, param->scan_rst.search_evt);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tBluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", event, param->scan_rst.search_evt, param->scan_rst.bda[0], param->scan_rst.bda[1], param->scan_rst.bda[2], param->scan_rst.bda[3], param->scan_rst.bda[4], param->scan_rst.bda[5]);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tBLE Device Type: %d", event, param->scan_rst.search_evt, param->scan_rst.dev_type);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tBLE Address Type: %d", event, param->scan_rst.search_evt, param->scan_rst.ble_addr_type);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tBLE Scan Result Event Type: %d", event, param->scan_rst.search_evt, param->scan_rst.ble_evt_type);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tPacket RSSI: %d dbm", event, param->scan_rst.search_evt, param->scan_rst.rssi);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tAdvertising Data Flag: 0x%02X", event, param->scan_rst.search_evt, param->scan_rst.flag);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tScan Response Count: %d", event, param->scan_rst.search_evt, param->scan_rst.num_resps);
            ESP_LOGI(LOG_TAG, "[%d][%d]\tDiscarded Packet Count: %lu", event, param->scan_rst.search_evt, param->scan_rst.num_dis);
#if CONFIG_EXAMPLE_DUMP_ADV_DATA_AND_SCAN_RESP
            if (param->scan_rst.adv_data_len > 0)
            {
                ESP_LOGI(LOG_TAG, "[%d][%d]\tRaw Advertising Data (%u):", event, param->scan_rst.search_evt, param->scan_rst.adv_data_len);
                esp_log_buffer_hex(LOG_TAG, &param->scan_rst.ble_adv[0], param->scan_rst.adv_data_len);
            }
            if (param->scan_rst.scan_rsp_len > 0)
            {
                ESP_LOGI(LOG_TAG, "[%d][%d]\tRaw Scan Response Data (%u):", event, param->scan_rst.search_evt, param->scan_rst.scan_rsp_len);
                esp_log_buffer_hex(LOG_TAG, &param->scan_rst.ble_adv[param->scan_rst.adv_data_len], param->scan_rst.scan_rsp_len);
            }
#endif
            ESP_LOGI(LOG_TAG, "[%d][%d]", event, param->scan_rst.search_evt);

            uint8_t *adv_data = NULL;
            uint8_t adv_data_len = 0;

            // Log Advertisement Data
            ESP_LOGI(LOG_TAG, "[%d][%d] Advertisement Data:", event, param->scan_rst.search_evt);

            // Log Flags
            adv_data = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_FLAG, &adv_data_len);
            ESP_LOGD(LOG_TAG, "[%d][%d]\tFlags Len: %d", event, param->scan_rst.search_evt, adv_data_len);
            ESP_LOGI(LOG_TAG, "[%d][%d]\t[0x%02X] Flag Value: 0x%02X", event, param->scan_rst.search_evt, ESP_BLE_AD_TYPE_FLAG, adv_data[0]);
            ESP_LOGI(LOG_TAG, "[%d][%d]\t\tLE Limited Discoverable Mode: %s", event, param->scan_rst.search_evt, (adv_data[0] & 0x01) ? "true" : "false");
            ESP_LOGI(LOG_TAG, "[%d][%d]\t\tLE General Discoverable Mode: %s", event, param->scan_rst.search_evt, (adv_data[0] & 0x02) ? "true" : "false");
            ESP_LOGI(LOG_TAG, "[%d][%d]\t\tBR/EDR Not Supported: %s", event, param->scan_rst.search_evt, (adv_data[0] & 0x04) ? "true" : "false");
            ESP_LOGI(LOG_TAG, "[%d][%d]\t\tSimultaneous LE and BR/EDR to Same Device Capable (Controller): %s", event, param->scan_rst.search_evt, (adv_data[0] & 0x08) ? "true" : "false");
            ESP_LOGI(LOG_TAG, "[%d][%d]\t\tSimultaneous LE and BR/EDR to Same Device Capable (Host): %s", event, param->scan_rst.search_evt, (adv_data[0] & 0x10) ? "true" : "false");

            // Log BLE Service UUIDs
            adv_data = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_16SRV_PART, &adv_data_len);
            if (adv_data_len > 0) {
                uint16_t ble_profile_uuid;
                ESP_LOGD(LOG_TAG, "[%d][%d]\tService UUIDs Len: %d", event, param->scan_rst.search_evt, adv_data_len);
                ESP_LOGI(LOG_TAG, "[%d][%d]\t[0x%02X] Partial 16-bit Service UUIDs:", event, param->scan_rst.search_evt, ESP_BLE_AD_TYPE_16SRV_PART);
                for (size_t i = 0; i < adv_data_len; i += 2)
                {
                    memcpy(&ble_profile_uuid, adv_data + i, sizeof(uint16_t));
                    ESP_LOGI(LOG_TAG, "[%d][%d]\t\t- 0x%04X", event, param->scan_rst.search_evt, ble_profile_uuid);
                }
            }

            // Log TX Power
            adv_data = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_TX_PWR, &adv_data_len);
            if (adv_data_len) {
                uint8_t tx_power;
                ESP_LOGD(LOG_TAG, "[%d][%d]\tTransmission Power Len: %d", event, param->scan_rst.search_evt, adv_data_len);
                memcpy(&tx_power, adv_data, adv_data_len);
                ESP_LOGI(LOG_TAG, "[%d][%d]\t[0x%02X] Transmission Power: %u", event, param->scan_rst.search_evt, ESP_BLE_AD_TYPE_TX_PWR, tx_power);
            }

            // Log Manufacturer Specific Data
            adv_data = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &adv_data_len);
            ESP_LOGD(LOG_TAG, "[%d][%d]\tManufacturer Specific Data Len: %d", event, param->scan_rst.search_evt, adv_data_len);
            ESP_LOGI(LOG_TAG, "[%d][%d]\t[0x%02X] Manufacturer Specific Data:", event, param->scan_rst.search_evt, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE);

            // Check if the manufacturer is OMRON
            const bool is_omron = ((adv_data_len > 2) && (OMRON_COMPANY_ID == *(uint16_t *)adv_data));
            if (is_omron && 6 <= adv_data_len)
            {
                uint16_t company_id;
                memcpy(&company_id, adv_data, sizeof(uint16_t));
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\tCompany Identifier Code: 0x%04X", event, param->scan_rst.search_evt, company_id);

                ESP_LOGI(LOG_TAG, "[%d][%d]\t\tOMRON Healthcare Specific Data Type: 0x%02X", event, param->scan_rst.search_evt, adv_data[2]);

                ESP_LOGI(LOG_TAG, "[%d][%d]\t\tUser Data:", event, param->scan_rst.search_evt);
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\tNumber of Users: %u (0x%02X)", event, param->scan_rst.search_evt, ((adv_data[3] & 0x03) + 1), (adv_data[3] & 0x03));
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\tTime Not Set: %s", event, param->scan_rst.search_evt, (adv_data[3] & 0x04) ? "true" : "false");
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\tPairing Mode: %s", event, param->scan_rst.search_evt, (adv_data[3] & 0x08) ? "Pairing" : "Transfer");
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\tStreaming: %s", event, param->scan_rst.search_evt, (adv_data[3] & 0x10) ? "Streaming" : "Data Communication");
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\tService UUID Mode: %s", event, param->scan_rst.search_evt, (adv_data[3] & 0x20) ? "WLP+STP" : "WLP");

                uint16_t sequence_no;
                memcpy(&sequence_no, &adv_data[4], sizeof(uint16_t));
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\tUser 1:", event, param->scan_rst.search_evt);
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\t\tSequence Number: %u", event, param->scan_rst.search_evt, sequence_no);
                ESP_LOGI(LOG_TAG, "[%d][%d]\t\t\t\tRecord Count: %u", event, param->scan_rst.search_evt, adv_data[5]);
            } else {
                esp_log_buffer_hex(LOG_TAG, adv_data, adv_data_len);
            }
            ESP_LOGI(LOG_TAG, "[%d][%d]", event, param->scan_rst.search_evt);

            // Log Scan Response
            if (param->scan_rst.scan_rsp_len > 0)
            {
                ESP_LOGI(LOG_TAG, "[%d][%d] Scan Response:", event, param->scan_rst.search_evt);

                // Log Device Name
                char device_name[32] = { 0 };
                adv_data = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                                    ESP_BLE_AD_TYPE_NAME_CMPL, &adv_data_len);
                ESP_LOGD(LOG_TAG, "[%d][%d]\tDevice Name Len: %d", event, param->scan_rst.search_evt, adv_data_len);
                adv_data[adv_data_len] = '\0';
                ESP_LOGI(LOG_TAG, "[%d][%d]\t[0x%02X] Device Name: %s", event, param->scan_rst.search_evt, ESP_BLE_AD_TYPE_NAME_CMPL, adv_data);
                if (is_omron) {
                    memcpy(device_name, adv_data, 9);
                    device_name[9] = '\0';
                    ESP_LOGI(LOG_TAG, "[%d][%d]\t\tLocal Name Prefix: %s", event, param->scan_rst.search_evt, device_name);
                    memcpy(device_name, &adv_data[9], 4);
                    device_name[4] = '\0';
                    ESP_LOGI(LOG_TAG, "[%d][%d]\t\tDevice Category: %s", event, param->scan_rst.search_evt, device_name);
                    memcpy(device_name, &adv_data[13], 4);
                    device_name[4] = '\0';
                    ESP_LOGI(LOG_TAG, "[%d][%d]\t\tDevice Model Type: %s", event, param->scan_rst.search_evt, device_name);
                    memcpy(device_name, &adv_data[17], 12);
                    device_name[12] = '\0';
                    ESP_LOGI(LOG_TAG, "[%d][%d]\t\tBD Address: %s", event, param->scan_rst.search_evt, device_name);
                }
                ESP_LOGI(LOG_TAG, "[%d][%d]", event, param->scan_rst.search_evt);

                // Initiate GATT Connection
                if ((adv_data != NULL) && ((char *)adv_data == strstr((char *)adv_data, remote_device_prefix))) {
                    ESP_LOGI(LOG_TAG, "[%d][%d] discovered a device matching \"%s\"", event, param->scan_rst.search_evt, remote_device_prefix);

                    if (connect) {
                        ESP_LOGI(LOG_TAG, "[%d][%d] already connecting to a remote device...", event, param->scan_rst.search_evt);
                    } else {
                        connect = true;

                        // Stop Scan
                        ESP_LOGI(LOG_TAG, "[%d][%d] stopping scan...", event, param->scan_rst.search_evt);
                        esp_ble_gap_stop_scanning();

                        // Connect to Device
                        ESP_LOGI(LOG_TAG, "[%d][%d] establishing direct connection to remote device %02X:%02X:%02X:%02X:%02X:%02X...", event, param->scan_rst.search_evt, param->scan_rst.bda[0], param->scan_rst.bda[1], param->scan_rst.bda[2], param->scan_rst.bda[3], param->scan_rst.bda[4], param->scan_rst.bda[5]);
                        esp_ble_gattc_open(gl_profile_tab[PROFILE_OMRON_ID].gattc_if, param->scan_rst.bda, param->scan_rst.ble_addr_type, true);
                    }
                }
            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            ESP_LOGI(LOG_TAG, "[%d] SEARCH_INQ_CMPL_EVT [%d]", event, param->scan_rst.search_evt);
            ESP_LOGI(LOG_TAG, "[%d][%d]", event, param->scan_rst.search_evt);

            // Report Scan Status
            ESP_LOGI(LOG_TAG, "[%d][%d] scan stopped (expired)", event, param->scan_rst.search_evt);
            break;
        default:
            ESP_LOGW(LOG_TAG, "[%d] UNHANDLED SEARCH_INQ EVENT [%d]", event, param->scan_rst.search_evt);
            ESP_LOGW(LOG_TAG, "[%d][%d]", event, param->scan_rst.search_evt);
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(LOG_TAG, "SEC_REQ_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", event, param->ble_security.ble_req.bd_addr[0], param->ble_security.ble_req.bd_addr[1], param->ble_security.ble_req.bd_addr[2], param->ble_security.ble_req.bd_addr[3], param->ble_security.ble_req.bd_addr[4], param->ble_security.ble_req.bd_addr[5]);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        // send the positive (true) security response to the peer device to
        // accept the security request. If not accept the security request,
        // should send the security response with negative (false) accept value
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGI(LOG_TAG, "KEY_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", event, param->ble_security.ble_key.bd_addr[0], param->ble_security.ble_key.bd_addr[1], param->ble_security.ble_key.bd_addr[2], param->ble_security.ble_key.bd_addr[3], param->ble_security.ble_key.bd_addr[4], param->ble_security.ble_key.bd_addr[5]);
        switch (param->ble_security.ble_key.key_type) {
            case ESP_LE_KEY_NONE:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_NONE", event);
                break;
            case ESP_LE_KEY_PENC:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PENC", event);
                break;
            case ESP_LE_KEY_PID:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PID", event);
                break;
            case ESP_LE_KEY_PCSRK:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PCSRK", event);
                break;
            case ESP_LE_KEY_PLK:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PLK", event);
                break;
            case ESP_LE_KEY_LLK:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LLK", event);
                break;
            case ESP_LE_KEY_LENC:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LENC", event);
                break;
            case ESP_LE_KEY_LID:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LID", event);
                break;
            case ESP_LE_KEY_LCSRK:
                ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LCSRK", event);
                break;
            default:
                ESP_LOGW(LOG_TAG, "[%d]\tKey Type: Unknown", event);
        }
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
    {
        ESP_LOGI(LOG_TAG, "AUTH_CMPL_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", event, param->ble_security.auth_cmpl.bd_addr[0], param->ble_security.auth_cmpl.bd_addr[1], param->ble_security.auth_cmpl.bd_addr[2], param->ble_security.auth_cmpl.bd_addr[3], param->ble_security.auth_cmpl.bd_addr[4], param->ble_security.auth_cmpl.bd_addr[5]);
        if (!param->ble_security.auth_cmpl.key_present) {
            ESP_LOGW(LOG_TAG, "[%d]\tNo Link Key Present", event);
        }
        else {
            ESP_LOGI(LOG_TAG, "[%d]\tLink Key:", event);
            ESP_LOG_BUFFER_HEX(LOG_TAG, param->ble_security.auth_cmpl.key, ESP_BT_OCTET16_LEN);
            switch (param->ble_security.auth_cmpl.key_type) {
                case ESP_LE_KEY_NONE:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_NONE", event);
                    break;
                case ESP_LE_KEY_PENC:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PENC", event);
                    break;
                case ESP_LE_KEY_PID:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PID", event);
                    break;
                case ESP_LE_KEY_PCSRK:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PCSRK", event);
                    break;
                case ESP_LE_KEY_PLK:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_PLK", event);
                    break;
                case ESP_LE_KEY_LLK:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LLK", event);
                    break;
                case ESP_LE_KEY_LENC:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LENC", event);
                    break;
                case ESP_LE_KEY_LID:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LID", event);
                    break;
                case ESP_LE_KEY_LCSRK:
                    ESP_LOGI(LOG_TAG, "[%d]\tKey Type: ESP_LE_KEY_LCSRK", event);
                    break;
                default:
                    ESP_LOGW(LOG_TAG, "[%d]\tKey Type: Unknown", event);
            }
/*
            // Save key to Non-volatile storage
            ESP_LOGI(LOG_TAG, "[%d]\tOpening Non-Volatile Storage (NVS) handle to save BLE key.", event);
            nvs_handle_t ble_handle;
            esp_err_t err = nvs_open("ble", NVS_READWRITE, &ble_handle);
            if (err != ESP_OK) {
                ESP_LOGE(LOG_TAG, "[%d]\tError opening NVS handle:\n\t%s", event, esp_err_to_name(err));
            } else {
                ESP_LOGI(LOG_TAG, "[%d]\tSuccessfully opened Non-Volatile Storage (NVS).", event);

                // Write BLE key to NVS
                if ((err = nvs_set_blob(ble_handle, "ble_key", param->ble_security.auth_cmpl.key, ESP_BT_OCTET16_LEN))) {
                    ESP_LOGE(LOG_TAG, "[%d]\tUnable to write BLE key to NVS:\n\t%s", event, esp_err_to_name(err));
                }
                // Write BLE key type to NVS
                else if ((err = nvs_set_u8(ble_handle, "ble_key_type", param->ble_security.auth_cmpl.key_type))) {
                    ESP_LOGE(LOG_TAG, "[%d]\tUnable to write BLE key type to NVS:\n\t%s", event, esp_err_to_name(err));
                }
                // Commit values to flash
                else if ((err = nvs_commit(ble_handle))) {
                    ESP_LOGE(LOG_TAG, "[%d]\tUnable to write BLE key type to NVS:\n\t%s", event, esp_err_to_name(err));
                }
                else {
                    ESP_LOGI(LOG_TAG, "[%d]\tSuccessfully saved BLE keys in Non-Volatile Storage (NVS).", event);
                }

                // Close
                nvs_close(ble_handle);
            }
*/
        }
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(LOG_TAG, "[%d]\tAuthentication Succeeded", event);
        }
        else {
            ESP_LOGE(LOG_TAG, "[%d]\tAuthentication Failed", event);
            ESP_LOGE(LOG_TAG, "[%d]\tHCI reason/error (auth failure): 0x%x", event, param->ble_security.auth_cmpl.fail_reason); // Reason/error values listed in esp_bt_defs.h:23
        }
        switch (param->ble_security.auth_cmpl.addr_type) {
            case BLE_ADDR_TYPE_PUBLIC:
                ESP_LOGI(LOG_TAG, "[%d]\tAddress Type: BLE_ADDR_TYPE_PUBLIC", event);
                break;
            case BLE_ADDR_TYPE_RANDOM:
                ESP_LOGI(LOG_TAG, "[%d]\tAddress Type: BLE_ADDR_TYPE_RANDOM", event);
                break;
            case BLE_ADDR_TYPE_RPA_PUBLIC:
                ESP_LOGI(LOG_TAG, "[%d]\tAddress Type: BLE_ADDR_TYPE_RPA_PUBLIC", event);
                break;
            case BLE_ADDR_TYPE_RPA_RANDOM:
                ESP_LOGI(LOG_TAG, "[%d]\tAddress Type: BLE_ADDR_TYPE_RPA_RANDOM", event);
                break;
            default:
                ESP_LOGW(LOG_TAG, "[%d]\tAddress Type: Unknown", event);
        }
        switch (param->ble_security.auth_cmpl.dev_type) {
            case ESP_BT_DEVICE_TYPE_BREDR:
                ESP_LOGI(LOG_TAG, "[%d]\tDevice Type: ESP_BT_DEVICE_TYPE_BREDR", event);
                break;
            case ESP_BT_DEVICE_TYPE_BLE:
                ESP_LOGI(LOG_TAG, "[%d]\tDevice Type: ESP_BT_DEVICE_TYPE_BLE", event);
                break;
            case ESP_BT_DEVICE_TYPE_DUMO:
                ESP_LOGI(LOG_TAG, "[%d]\tDevice Type: ESP_BT_DEVICE_TYPE_DUMO", event);
                break;
            default:
                ESP_LOGW(LOG_TAG, "[%d]\tDevice Type: Unknown", event);
        }
        switch (param->ble_security.auth_cmpl.auth_mode) {
            case ESP_LE_AUTH_NO_BOND:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_NO_BOND", event);
                break;
            case ESP_LE_AUTH_BOND:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_BOND", event);
                break;
            case ESP_LE_AUTH_REQ_MITM:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_REQ_MITM", event);
                break;
            case ESP_LE_AUTH_REQ_BOND_MITM:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_REQ_BOND_MITM", event);
                break;
            case ESP_LE_AUTH_REQ_SC_ONLY:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_REQ_SC_ONLY", event);
                break;
            case ESP_LE_AUTH_REQ_SC_BOND:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_REQ_SC_BOND", event);
                break;
            case ESP_LE_AUTH_REQ_SC_MITM:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_REQ_SC_MITM", event);
                break;
            case ESP_LE_AUTH_REQ_SC_MITM_BOND:
                ESP_LOGI(LOG_TAG, "[%d]\tAuth Mode: ESP_LE_AUTH_REQ_SC_MITM_BOND", event);
                break;
            default:
                ESP_LOGW(LOG_TAG, "[%d]\tAuth Mode: Unknown", event);
        }
        ESP_LOGI(LOG_TAG, "[%d]", event);
        break;
    }

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        if (ESP_BT_STATUS_SUCCESS != param->update_conn_params.status) {
            ESP_LOGE(LOG_TAG, "UPDATE_CONN_PARAMS_EVT [%d]", event);
            ESP_LOGE(LOG_TAG, "[%d]\tStatus: %d", event, param->update_conn_params.status);
            ESP_LOGE(LOG_TAG, "[%d]", event);
            break;
        }

        ESP_LOGI(LOG_TAG, "UPDATE_CONN_PARAMS_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tStatus: %d", event, param->update_conn_params.status);
        ESP_LOGI(LOG_TAG, "[%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", event, param->update_conn_params.bda[0], param->update_conn_params.bda[1], param->update_conn_params.bda[2], param->update_conn_params.bda[3], param->update_conn_params.bda[4], param->update_conn_params.bda[5]);
        ESP_LOGI(LOG_TAG, "[%d]\tMinimum Connection Interval: %u", event, param->update_conn_params.min_int);
        ESP_LOGI(LOG_TAG, "[%d]\tMaximum Connection Interval: %u", event, param->update_conn_params.max_int);
        ESP_LOGI(LOG_TAG, "[%d]\tSlave Latency (number of connection events): 0x%04x", event, param->update_conn_params.latency);
        ESP_LOGI(LOG_TAG, "[%d]\tConnection Interval: %u", event, param->update_conn_params.conn_int);
        ESP_LOGI(LOG_TAG, "[%d]\tLink Timeout: %u0ms", event, param->update_conn_params.timeout);
        ESP_LOGI(LOG_TAG, "[%d]", event);
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (ESP_BT_STATUS_SUCCESS != param->scan_stop_cmpl.status) {
            ESP_LOGE(LOG_TAG, "SCAN_STOP_COMPLETE_EVT [%d]", event);
            ESP_LOGE(LOG_TAG, "[%d]\tStatus: %d", event, param->scan_stop_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%d]", event);
            break;
        }

        ESP_LOGI(LOG_TAG, "SCAN_STOP_COMPLETE_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tStatus: %d", event, param->scan_stop_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        // Report Scan Status
        ESP_LOGI(LOG_TAG, "[%d] scan stopped", event);
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (ESP_BT_STATUS_SUCCESS != param->adv_stop_cmpl.status) {
            ESP_LOGE(LOG_TAG, "ADV_STOP_COMPLETE_EVT [%d]", event);
            ESP_LOGE(LOG_TAG, "[%d]\tStatus: %d", event, param->adv_stop_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%d]", event);
            break;
        }

        ESP_LOGI(LOG_TAG, "ADV_STOP_COMPLETE_EVT [%d]", event);
        ESP_LOGI(LOG_TAG, "[%d]\tStatus: %d", event, param->adv_stop_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%d]", event);

        // Report the result of the advertising stop command
        ESP_LOGI(LOG_TAG, "[%d] stop adv successfully", event);
        break;
    default:
        ESP_LOGW(LOG_TAG, "UNHANDLED EVENT [%d]", event);
        ESP_LOGW(LOG_TAG, "[%d]", event);
        break;
    }
}
