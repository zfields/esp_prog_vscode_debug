#include "omron_gatt_scan.h"

// Include C standard libraries
#include <string.h>

// Include ESP-IDF libraries
#include <esp_gap_ble_api.h>
#include <esp_gatt_common_api.h>
#include <esp_log.h>

// Include Notecard note-c library
#include <note.h>

// Include OMRON BLE files
#include "omron_defs.h"
#include "omron_gap_scan.h"

#define LOG_TAG "GATT_SCAN"

typedef struct esp_gattc_service_elem_node_t {
    esp_gattc_service_elem_t element;
    struct esp_gattc_service_elem_node_t *next;
} esp_gattc_service_elem_node_t;

typedef struct {
    esp_gattc_char_elem_t element;
    uint16_t value_len;
    uint8_t *value;
} characteristic_t;

struct gattc_profile_inst gl_profile_tab[PROFILE_COUNT] = {
    [PROFILE_OMRON_ID] = {
        .gattc_cb = esp_gattc_common_cb,
        .gattc_if = ESP_GATT_IF_NONE /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    }
};

static esp_gattc_service_elem_node_t *service_elem_list = NULL;
static esp_gattc_char_elem_t *char_elem_result = NULL;
static esp_gattc_descr_elem_t *descr_elem_result = NULL;

static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}
};

volatile static bool fetch_characteristics = false;

void esp_gattc_common_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    ESP_LOGI(LOG_TAG, "Interface Type: %u", gattc_if);
    switch (event)
    {
    case ESP_GATTC_REG_EVT:
    {
        if (ESP_GATT_OK != param->reg.status)
        {
            ESP_LOGE(LOG_TAG, "[%u] REG_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->reg.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tApplication ID: 0x%04x", gattc_if, event, param->reg.app_id);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] REG_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->reg.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tApplication ID: 0x%04x", gattc_if, event, param->reg.app_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Report that the application is now registered
        ESP_LOGI(LOG_TAG, "[%u][%d] application 0x%04x, now registered", gattc_if, event, param->reg.app_id);

        // Establish the Maximum Transmission Unit (MTU) size
        ESP_LOGI(LOG_TAG, "[%u][%d] configuring local MTU size: %u", gattc_if, event, REMOTE_BLE_4_0_MTU_SIZE);
        esp_err_t err = esp_ble_gatt_set_local_mtu(REMOTE_BLE_4_0_MTU_SIZE);
        if (err)
        {
            ESP_LOGE(LOG_TAG, "config local MTU failed, error code = 0x%x", err);
        }

        break;
    }
    case ESP_GATTC_CONNECT_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] CONNECT_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->connect.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tLink Role: %s", gattc_if, event, (param->connect.conn_id ? "Slave" : "Master"));
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", gattc_if, event, param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2], param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection Interval: %u", gattc_if, event, param->connect.conn_params.interval);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection Latency: %u", gattc_if, event, param->connect.conn_params.latency);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection Timeout: %u0 ms", gattc_if, event, param->connect.conn_params.timeout);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Copy the connection information to the profile table
        gl_profile_tab[PROFILE_OMRON_ID].conn_id = param->connect.conn_id;
        memcpy(gl_profile_tab[PROFILE_OMRON_ID].remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));

        // Configure the MTU size
        ESP_LOGI(LOG_TAG, "[%u][%d] configuring the MTU size for connection %u on interface %u...", gattc_if, event, param->connect.conn_id, gattc_if);
        esp_err_t err = esp_ble_gattc_send_mtu_req(gattc_if, param->connect.conn_id);
        if (err)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] config MTU error, error code = 0x%x", gattc_if, event, err);
        }
        break;
    }
    case ESP_GATTC_OPEN_EVT:
    {
        if (ESP_GATT_OK != param->open.status) {
            ESP_LOGE(LOG_TAG, "[%u] OPEN_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->open.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] OPEN_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->open.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->open.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", gattc_if, event, param->open.remote_bda[0], param->open.remote_bda[1], param->open.remote_bda[2], param->open.remote_bda[3], param->open.remote_bda[4], param->open.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tMTU Size: %u", gattc_if, event, param->open.mtu);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Report that the connection is now open
        ESP_LOGI(LOG_TAG, "[%u][%d] connection %u is now open", gattc_if, event, param->open.conn_id);
        break;
    }
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
    {
        if (ESP_GATT_OK != param->dis_srvc_cmpl.status) {
            ESP_LOGE(LOG_TAG, "[%u] DIS_SRVC_CMPL_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->dis_srvc_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] DIS_SRVC_CMPL_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->dis_srvc_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->dis_srvc_cmpl.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Report that the service discovery is now complete
        ESP_LOGI(LOG_TAG, "[%u][%d] service discovery is now complete for connection %u", gattc_if, event, param->dis_srvc_cmpl.conn_id);
        break;
    }
    case ESP_GATTC_CFG_MTU_EVT:
    {
        if (ESP_GATT_OK != param->cfg_mtu.status) {
            ESP_LOGE(LOG_TAG, "[%u] CFG_MTU_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->cfg_mtu.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] CFG_MTU_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->cfg_mtu.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->cfg_mtu.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tMTU Size: %u", gattc_if, event, param->cfg_mtu.mtu);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Report that the MTU size has been configured
        ESP_LOGI(LOG_TAG, "[%u][%d] Established MTU as %d for connection %u", gattc_if, event, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        break;
    }
    case ESP_GATTC_SEARCH_RES_EVT: // callback spawned from calling `esp_ble_gattc_search_service()`
    {
        ESP_LOGI(LOG_TAG, "[%u] SEARCH_RES_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->search_res.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStart Handle: %u", gattc_if, event, param->search_res.start_handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tEnd Handle: %u", gattc_if, event, param->search_res.end_handle);
        switch (param->search_res.srvc_id.uuid.len)
        {
        case ESP_UUID_LEN_16:
            ESP_LOGI(LOG_TAG, "[%u][%d]\tService UUID: 0x%04X", gattc_if, event, param->search_res.srvc_id.uuid.uuid.uuid16);
            break;
        case ESP_UUID_LEN_32:
            ESP_LOGI(LOG_TAG, "[%u][%d]\tService UUID: 0x%08lX", gattc_if, event, param->search_res.srvc_id.uuid.uuid.uuid32);
            break;
        case ESP_UUID_LEN_128:
            ESP_LOGI(LOG_TAG, "[%u][%d]\tService UUID: %08lX-%04X-%04X-%04X-%012llX", gattc_if, event, *(uint32_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[12], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[10], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[8], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[6], *(uint64_t *)param->search_res.srvc_id.uuid.uuid.uuid128 & 0x0000FFFFFFFFFFFF);
            break;
        };
        ESP_LOGI(LOG_TAG, "[%u][%d]\tService ID Instance: %u", gattc_if, event, param->search_res.srvc_id.inst_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tIs Primary Service: %u", gattc_if, event, param->search_res.is_primary);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        switch (param->search_res.srvc_id.uuid.len)
        {
        case ESP_UUID_LEN_16:
            ESP_LOGI(LOG_TAG, "[%u][%d] service (0x%04X) discovery underway...", gattc_if, event, param->search_res.srvc_id.uuid.uuid.uuid16);
            break;
        case ESP_UUID_LEN_32:
            ESP_LOGI(LOG_TAG, "[%u][%d] service (0x%08lX) discovery underway...", gattc_if, event, param->search_res.srvc_id.uuid.uuid.uuid32);
            break;
        case ESP_UUID_LEN_128:
            ESP_LOGI(LOG_TAG, "[%u][%d] service (%08lX-%04X-%04X-%04X-%012llX) discovery underway...", gattc_if, event, *(uint32_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[12], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[10], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[8], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[6], *(uint64_t *)param->search_res.srvc_id.uuid.uuid.uuid128 & 0x0000FFFFFFFFFFFF);
            break;
        };

        // Copy the service information to the global profile table
        gl_profile_tab[PROFILE_OMRON_ID].service_start_handle = param->search_res.start_handle;
        gl_profile_tab[PROFILE_OMRON_ID].service_end_handle = param->search_res.end_handle;

        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: // Called when all callbacks spawned from `esp_ble_gattc_search_service()` have completed
    {
        if (ESP_GATT_OK != param->search_cmpl.status) {
            ESP_LOGE(LOG_TAG, "[%u] SEARCH_CMPL_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->search_cmpl.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] SEARCH_CMPL_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->search_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->search_cmpl.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tSearched Service Source: %u", gattc_if, event, param->search_cmpl.searched_service_source);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Shuffle Nodes
        esp_gattc_service_elem_node_t * n1810 = service_elem_list;
        esp_gattc_service_elem_node_t * n1805 = service_elem_list->next;
        esp_gattc_service_elem_node_t * n180F = service_elem_list->next->next;
        service_elem_list = n1805;
        n1805->next = n180F;
        n180F->next = n1810;
        n1810->next = NULL;

        // Iterate through the service element list
        for (esp_gattc_service_elem_node_t * n = service_elem_list ; n ; n = n->next) {
            uint16_t char_count = 0;
            switch (n->element.uuid.len)
            {
            case ESP_UUID_LEN_16:
                ESP_LOGI(LOG_TAG, "[%u][%d] Service UUID: 0x%04X", gattc_if, event, n->element.uuid.uuid.uuid16);
                break;
            case ESP_UUID_LEN_32:
                ESP_LOGI(LOG_TAG, "[%u][%d] Service UUID: 0x%08lX", gattc_if, event, n->element.uuid.uuid.uuid32);
                break;
            case ESP_UUID_LEN_128:
                ESP_LOGI(LOG_TAG, "[%u][%d] Service UUID: %08lX-%04X-%04X-%04X-%012llX", gattc_if, event, *(uint32_t *)&n->element.uuid.uuid.uuid128[12], *(uint16_t *)&n->element.uuid.uuid.uuid128[10], *(uint16_t *)&n->element.uuid.uuid.uuid128[8], *(uint16_t *)&n->element.uuid.uuid.uuid128[6], *(uint64_t *)n->element.uuid.uuid.uuid128 & 0x0000FFFFFFFFFFFF);
                break;
            };
            ESP_LOGI(LOG_TAG, "[%u][%d] getting characteristic count of service...", gattc_if, event);
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                    param->search_cmpl.conn_id,
                                                                    ESP_GATT_DB_CHARACTERISTIC,
                                                                    n->element.start_handle,
                                                                    n->element.end_handle,
                                                                    IGNORED_PARAMETER,
                                                                    &char_count);
            if (ESP_GATT_OK != status) {
                ESP_LOGE(LOG_TAG, "[%u][%d] failed to get characteristic count, error code = 0x%x", gattc_if, event, status);
                break;
            }

            if (char_count > 0) {
                ESP_LOGI(LOG_TAG, "[%u][%d] characteristic count: %u", gattc_if, event, char_count);

                // Allocate memory for the characteristic element array
                ESP_LOGD(LOG_TAG, "[%u][%d] allocating characteristic array...", gattc_if, event);
                char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * char_count);
                if (!char_elem_result) {
                    ESP_LOGE(LOG_TAG, "[%u][%d] unable to allocate characteristic element array", gattc_if, event);
                    break;
                }
                ESP_LOGD(LOG_TAG, "[%u][%d] allocated characteristic array for %u items", gattc_if, event, char_count);

                // Populate the characteristic element array
                ESP_LOGI(LOG_TAG, "[%u][%d] populating characteristic array...", gattc_if, event);
                status = esp_ble_gattc_get_all_char(gattc_if,
                                            param->search_cmpl.conn_id,
                                            n->element.start_handle,
                                            n->element.end_handle,
                                            char_elem_result,
                                            &char_count,
                                            OFFSET_ZERO);
                if (ESP_GATT_OK != status) {
                    ESP_LOGE(LOG_TAG, "[%u][%d] failed to populate characteristic array, error code = 0x%x", gattc_if, event, status);
                    break;
                }
                ESP_LOGI(LOG_TAG, "[%u][%d] populated characteristic array with %u items", gattc_if, event, char_count);

                for (size_t i = 0; i < char_count; ++i) {
                    ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
                    ESP_LOGI(LOG_TAG, "[%u][%d] Characteristic Handle: %u", gattc_if, event, char_elem_result[i].char_handle);
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u] Properties: 0x%02X", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties);
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tBroadcast: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_BROADCAST ? "true" : "false");
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tRead: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_READ ? "true" : "false");
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tWrite No Response: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR ? "true" : "false");
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tWrite: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_WRITE ? "true" : "false");
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tNotify: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY ? "true" : "false");
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tIndicate: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_INDICATE ? "true" : "false");
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tSigned Write: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_AUTH ? "true" : "false");
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]\tExtended Properties: %s", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].properties & ESP_GATT_CHAR_PROP_BIT_EXT_PROP ? "true" : "false");
                    switch (char_elem_result[i].uuid.len)
                    {
                    case ESP_UUID_LEN_16:
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] Characteristic UUID: 0x%04X", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].uuid.uuid.uuid16);
                        break;
                    case ESP_UUID_LEN_32:
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] Characteristic UUID: 0x%08lX", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].uuid.uuid.uuid32);
                        break;
                    case ESP_UUID_LEN_128:
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] Characteristic UUID: %08lX-%04X-%04X-%04X-%012llX", gattc_if, event, char_elem_result[i].char_handle, *(uint32_t *)&char_elem_result[i].uuid.uuid.uuid128[12], *(uint16_t *)&char_elem_result[i].uuid.uuid.uuid128[10], *(uint16_t *)&char_elem_result[i].uuid.uuid.uuid128[8], *(uint16_t *)&char_elem_result[i].uuid.uuid.uuid128[6], *(uint64_t *)char_elem_result[i].uuid.uuid.uuid128 & 0x0000FFFFFFFFFFFF);
                        break;
                    };
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u]", gattc_if, event, char_elem_result[i].char_handle);

                    // Interact with descriptors of the characteristic
                    uint16_t descr_count = 0;
                    ESP_LOGI(LOG_TAG, "[%u][%d][%u] getting descriptor count of characteristic...", gattc_if, event, char_elem_result[i].char_handle);
                    esp_gatt_status_t status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                            param->search_cmpl.conn_id,
                                                                            ESP_GATT_DB_DESCRIPTOR,
                                                                            IGNORED_PARAMETER,
                                                                            IGNORED_PARAMETER,
                                                                            char_elem_result[i].char_handle,
                                                                            &descr_count);
                    if (ESP_GATT_OK != status) {
                        ESP_LOGE(LOG_TAG, "[%u][%d][%u] failed to get descriptor count, error code = 0x%x", gattc_if, event, char_elem_result[i].char_handle, status);
                        break;
                    }

                    if (descr_count > 0) {
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] descriptor count: %u", gattc_if, event, char_elem_result[i].char_handle, descr_count);

                        // Allocate memory for the descriptor element array
                        ESP_LOGD(LOG_TAG, "[%u][%d][%u] allocating descriptor array...", gattc_if, event, char_elem_result[i].char_handle);
                        descr_elem_result = (esp_gattc_descr_elem_t *)malloc(sizeof(esp_gattc_descr_elem_t) * descr_count);
                        if (!descr_elem_result) {
                            ESP_LOGE(LOG_TAG, "[%u][%d][%u] unable to allocate descriptor array", gattc_if, event, char_elem_result[i].char_handle);
                            break;
                        }
                        ESP_LOGD(LOG_TAG, "[%u][%d][%u] allocated descriptor array for %u items", gattc_if, event, char_elem_result[i].char_handle, descr_count);

                        // Populate the descriptor element array
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] populating descriptor array...", gattc_if, event, char_elem_result[i].char_handle);
                        status = esp_ble_gattc_get_all_descr(gattc_if,
                                                    param->search_cmpl.conn_id,
                                                    char_elem_result[i].char_handle,
                                                    descr_elem_result,
                                                    &descr_count,
                                                    OFFSET_ZERO);
                        if (ESP_GATT_OK != status) {
                            ESP_LOGE(LOG_TAG, "[%u][%d][%u] failed to populate descriptor array, error code = 0x%x", gattc_if, event, char_elem_result[i].char_handle, status);
                            free(descr_elem_result);
                            descr_elem_result = NULL;
                            ESP_LOGD(LOG_TAG, "[%u][%d][%u] freed descriptor array", gattc_if, event, char_elem_result[i].char_handle);
                            break;
                        }
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] populated descriptor array with %u items", gattc_if, event, char_elem_result[i].char_handle, descr_count);

                        for (size_t j = 0; j < descr_count; ++j) {
                            ESP_LOGI(LOG_TAG, "[%u][%d][%u]", gattc_if, event, char_elem_result[i].char_handle);
                            ESP_LOGI(LOG_TAG, "[%u][%d][%u] Descriptor Handle: %u", gattc_if, event, char_elem_result[i].char_handle, descr_elem_result[j].handle);
                            switch (descr_elem_result[j].uuid.len)
                            {
                            case ESP_UUID_LEN_16:
                                ESP_LOGI(LOG_TAG, "[%u][%d][%u][%u] Descriptor UUID: 0x%04X", gattc_if, event, char_elem_result[i].char_handle, descr_elem_result[j].handle, descr_elem_result[j].uuid.uuid.uuid16);
                                break;
                            case ESP_UUID_LEN_32:
                                ESP_LOGI(LOG_TAG, "[%u][%d][%u][%u] Descriptor UUID: 0x%08lX", gattc_if, event, char_elem_result[i].char_handle, descr_elem_result[j].handle, descr_elem_result[j].uuid.uuid.uuid32);
                                break;
                            case ESP_UUID_LEN_128:
                                ESP_LOGI(LOG_TAG, "[%u][%d][%u][%u] Descriptor UUID: %08lX-%04X-%04X-%04X-%012llX", gattc_if, event, char_elem_result[i].char_handle, descr_elem_result[j].handle, *(uint32_t *)&descr_elem_result[j].uuid.uuid.uuid128[12], *(uint16_t *)&descr_elem_result[j].uuid.uuid.uuid128[10], *(uint16_t *)&descr_elem_result[j].uuid.uuid.uuid128[8], *(uint16_t *)&descr_elem_result[j].uuid.uuid.uuid128[6], *(uint64_t *)descr_elem_result[j].uuid.uuid.uuid128 & 0x0000FFFFFFFFFFFF);
                                break;
                            };
                            ESP_LOGI(LOG_TAG, "[%u][%d][%u][%u]", gattc_if, event, char_elem_result[i].char_handle, descr_elem_result[j].handle);

                            // Write to the Characterist Client Config descriptor
                            if (ESP_UUID_LEN_16 == descr_elem_result[j].uuid.len && ESP_GATT_UUID_CHAR_CLIENT_CONFIG == descr_elem_result[j].uuid.uuid.uuid16)
                            {
                                uint16_t cccd;
                                if (ESP_GATT_CHAR_PROP_BIT_INDICATE & char_elem_result[i].properties) {
                                    cccd = CCCD_INDICATION_ENABLED;
                                } else if (ESP_GATT_CHAR_PROP_BIT_NOTIFY & char_elem_result[i].properties) {
                                    cccd = CCCD_NOTIFICATION_ENABLED;
                                } else {
                                    ESP_LOGW(LOG_TAG, "[%u][%d] characteristic does not have NOTIFY or INDICATE property", gattc_if, event);
                                    break;
                                }

                                ESP_LOGI(LOG_TAG, "[%u][%d] enable the indication on the CCCD descriptor %d...", gattc_if, event, descr_elem_result[j].handle);
                                status = esp_ble_gattc_write_char_descr(gattc_if,
                                                                        gl_profile_tab[PROFILE_OMRON_ID].conn_id,
                                                                        descr_elem_result[j].handle,
                                                                        sizeof(cccd),
                                                                        (uint8_t *)&cccd,
                                                                        ESP_GATT_WRITE_TYPE_RSP,
                                                                        ESP_GATT_AUTH_REQ_NONE);
                                if (ESP_GATT_OK != status) {
                                    ESP_LOGE(LOG_TAG, "[%u][%d] failed to enable the notification, error code = 0x%x", gattc_if, event, status);
                                    break;
                                }
                            }

                            ESP_LOGI(LOG_TAG, "[%u][%d][%u][%u] reading descriptor %d on connection %u on interface %u...", gattc_if, event, char_elem_result[i].char_handle, descr_elem_result[j].handle, descr_elem_result[j].handle, param->search_cmpl.conn_id, gattc_if);
                            esp_err_t err = esp_ble_gattc_read_char_descr(gattc_if, param->search_cmpl.conn_id, descr_elem_result[j].handle, ESP_GATT_AUTH_REQ_NONE);
                            if (ESP_OK != err)
                            {
                                ESP_LOGE(LOG_TAG, "[%u][%d][%u][%u] failed to read descriptor, error code = 0x%x", gattc_if, event, char_elem_result[i].char_handle, descr_elem_result[j].handle, err);
                            }
                        }

                        // Free the descriptor element array
                        free(descr_elem_result);
                        descr_elem_result = NULL;
                        ESP_LOGD(LOG_TAG, "[%u][%d][%u] freed descriptor array", gattc_if, event, char_elem_result[i].char_handle);
                    }
                    else {
                        ESP_LOGW(LOG_TAG, "[%u][%d][%u] no descriptors found", gattc_if, event, char_elem_result[i].char_handle);
                    }

                    // Interact with properties of the characteristic
                    if (ESP_GATT_CHAR_PROP_BIT_BROADCAST & char_elem_result[i].properties) {
                        ESP_LOGW(LOG_TAG, "[%u][%d][%u] characteristic has property BROADCAST, but logic not implemented", gattc_if, event, char_elem_result[i].char_handle);
                    }
                    if (ESP_GATT_CHAR_PROP_BIT_READ & char_elem_result[i].properties)
                    {
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] reading characteristic %d on connection %u on interface %u...", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].char_handle, param->search_cmpl.conn_id, gattc_if);
                        esp_err_t err = esp_ble_gattc_read_char(gattc_if, param->search_cmpl.conn_id, char_elem_result[i].char_handle, ESP_GATT_AUTH_REQ_NONE);
                        if (ESP_OK != err)
                        {
                            ESP_LOGE(LOG_TAG, "[%u][%d][%u] failed to read characteristic, error code = 0x%x", gattc_if, event, char_elem_result[i].char_handle, err);
                        }
                    }
                    if (ESP_GATT_CHAR_PROP_BIT_WRITE_NR & char_elem_result[i].properties) {
                        ESP_LOGW(LOG_TAG, "[%u][%d][%u] characteristic has property WRITE NO RESPONSE, but logic not implemented", gattc_if, event, char_elem_result[i].char_handle);
                    }
                    if (ESP_GATT_CHAR_PROP_BIT_WRITE & char_elem_result[i].properties) {
                        ESP_LOGW(LOG_TAG, "[%u][%d][%u] characteristic has property WRITE, but logic not implemented", gattc_if, event, char_elem_result[i].char_handle);
                    }
                    if (ESP_GATT_CHAR_PROP_BIT_NOTIFY & char_elem_result[i].properties)
                    {
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] registering for notification from characteristic %d on interface %u of device %02X:%02X:%02X:%02X:%02X:%02X...", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].char_handle, gattc_if, gl_profile_tab[PROFILE_OMRON_ID].remote_bda[0], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[1], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[2], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[3], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[4], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[5]);
                        esp_err_t err = esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_OMRON_ID].remote_bda, char_elem_result[i].char_handle);
                        if (ESP_OK != err)
                        {
                            ESP_LOGE(LOG_TAG, "[%u][%d][%u] failed to register for notification, error code = 0x%x", gattc_if, event, char_elem_result[i].char_handle, err);
                        }
                    }
                    if (ESP_GATT_CHAR_PROP_BIT_INDICATE & char_elem_result[i].properties) {
                        // ESP_LOGW(LOG_TAG, "[%u][%d][%u] characteristic has property INDICATE, but logic not implemented", gattc_if, event, char_elem_result[i].char_handle);
                        ESP_LOGI(LOG_TAG, "[%u][%d][%u] register for indications from characteristic %d on interface %u of device %02X:%02X:%02X:%02X:%02X:%02X...", gattc_if, event, char_elem_result[i].char_handle, char_elem_result[i].char_handle, gattc_if, gl_profile_tab[PROFILE_OMRON_ID].remote_bda[0], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[1], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[2], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[3], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[4], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[5]);
                        esp_err_t err = esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_OMRON_ID].remote_bda, char_elem_result[i].char_handle);
                        if (ESP_OK != err)
                        {
                            ESP_LOGE(LOG_TAG, "[%u][%d][%u] failed to register for indications, error code = 0x%x", gattc_if, event, char_elem_result[i].char_handle, err);
                        }
                    }
                    if (ESP_GATT_CHAR_PROP_BIT_AUTH & char_elem_result[i].properties) {
                        ESP_LOGW(LOG_TAG, "[%u][%d][%u] characteristic has property AUTH, but logic not implemented", gattc_if, event, char_elem_result[i].char_handle);
                    }
                    if (ESP_GATT_CHAR_PROP_BIT_EXT_PROP & char_elem_result[i].properties) {
                        ESP_LOGW(LOG_TAG, "[%u][%d][%u] characteristic has property EXT PROP, but logic not implemented", gattc_if, event, char_elem_result[i].char_handle);
                    }
                }
                // Free the characteristic element array
                free(char_elem_result);
                char_elem_result = NULL;
                ESP_LOGD(LOG_TAG, "[%u][%d] freed characteristic array", gattc_if, event);
            }
            else
            {
                ESP_LOGW(LOG_TAG, "[%u][%d] no characteristics found", gattc_if, event);
            }
            ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        }
        break;
    }
    case ESP_GATTC_READ_DESCR_EVT:
    {
        if (ESP_GATT_OK != param->read.status) {
            ESP_LOGE(LOG_TAG, "[%u] READ_DESCR_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->read.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] READ_DESCR_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->read.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->read.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->read.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tValue (%u):", gattc_if, event, param->read.value_len);
        esp_log_buffer_hex(LOG_TAG, param->read.value, param->read.value_len);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        break;
    }
    case ESP_GATTC_READ_CHAR_EVT:
    {
        if (ESP_GATT_OK != param->read.status) {
            ESP_LOGE(LOG_TAG, "[%u] READ_CHAR_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->read.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->read.conn_id);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->read.handle);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] READ_CHAR_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->read.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->read.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->read.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tValue (%u):", gattc_if, event, param->read.value_len);
        esp_log_buffer_hex(LOG_TAG, param->read.value, param->read.value_len);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // size_t load_count = 0;
        // for (size_t i = 0; i < DIS_CHAR_COUNT; ++i) {
        //     if (!char_elem_result[i].value_loaded && param->read.handle == char_elem_result[i].char_handle) {
        //         // Load the value into the struct
        //         char_elem_result[i].value_len = param->read.value_len;
        //         char_elem_result[i].value = (uint8_t *)malloc(sizeof(uint8_t) * char_elem_result[i].value_len + sizeof(uint8_t));
        //         char_elem_result[i].value = memcpy(char_elem_result[i].value, param->read.value, char_elem_result[i].value_len);
        //         char_elem_result[i].value[char_elem_result[i].value_len] = '\0';
        //         char_elem_result[i].value_loaded = true;

        //         // Add to the load count
        //         ++load_count;
        //         ESP_LOGI(LOG_TAG, "[%u][%d] characteristic value saved for 0x%X", gattc_if, event, char_elem_result[i].uuid.uuid.uuid16);
        //     } else if (char_elem_result[i].value_loaded) {
        //         // Already loaded, count in the load count
        //         ++load_count;
        //     }
        // }

        // if (load_count == DIS_CHAR_COUNT) {
        //     load_count = 0;
        //     ESP_LOGI(LOG_TAG, "[%u][%d] all characteristic values loaded, and ready for transmission.", gattc_if, event);

        //     // Send a Note to Notehub
        //     J *req;
        //     if ((req = NoteNewRequest("note.add")))
        //     {
        //         JAddStringToObject(req, "file", "omron.qo");
        //         JAddBoolToObject(req, "sync", true);
        //         J *body = JAddObjectToObject(req, "body");
        //         if (body) {
        //             char uuid16_buffer[8];
        //             char note_buffer[32];
        //             snprintf(note_buffer, sizeof(note_buffer), "%02X:%02X:%02X:%02X:%02X:%02X", gl_profile_tab[PROFILE_OMRON_ID].remote_bda[0], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[1], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[2], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[3], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[4], gl_profile_tab[PROFILE_OMRON_ID].remote_bda[5]);
        //             JAddStringToObject(body, "Bluetooth Device Address", note_buffer);
        //             ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: Bluetooth Device Address: %s", gattc_if, event, note_buffer);
        //             snprintf(uuid16_buffer, sizeof(uuid16_buffer), "0x%04X", ESP_GATT_UUID_DEVICE_INFO_SVC);
        //             JAddStringToObject(body, "GATT Service", uuid16_buffer);
        //             ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: GATT Service: %s", gattc_if, event, uuid16_buffer);
        //             for (size_t i = 0; i < DIS_CHAR_COUNT; ++i) {
        //                 ESP_LOGI(LOG_TAG, "[%u][%d] Characteristic (0x%X) data on Handle %d, Len %d, Value:", gattc_if, event, char_elem_result[i].uuid.uuid.uuid16, char_elem_result[i].char_handle, char_elem_result[i].value_len);
        //                 snprintf(uuid16_buffer, sizeof(uuid16_buffer), "0x%04X", char_elem_result[i].uuid.uuid.uuid16);
        //                 if (ESP_GATT_UUID_SYSTEM_ID == char_elem_result[i].uuid.uuid.uuid16
        //                     || ESP_GATT_UUID_IEEE_DATA == char_elem_result[i].uuid.uuid.uuid16) {
        //                     esp_log_buffer_hex(LOG_TAG, char_elem_result[i].value, char_elem_result[i].value_len);
        //                     note_buffer[0] = '0';
        //                     note_buffer[1] = 'x';
        //                     note_buffer[2] = '\0';
        //                     for (size_t j = 0; j < char_elem_result[i].value_len; ++j) {
        //                         sniprintf(note_buffer, sizeof(note_buffer), "%s%02X", note_buffer, char_elem_result[i].value[j]);
        //                     }
        //                 } else {
        //                     ESP_LOGI(LOG_TAG, "[%u][%d] %s", gattc_if, event, char_elem_result[i].value);
        //                     strncpy(note_buffer, (const char *)char_elem_result[i].value, sizeof(note_buffer));
        //                 }
        //                 JAddStringToObject(body, uuid16_buffer, note_buffer);
        //                 ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: %s: %s", gattc_if, event, uuid16_buffer, note_buffer);
        //                 free(char_elem_result[i].value);
        //             }
        //             free(char_elem_result);
        //             if (!NoteRequest(req))
        //             {
        //                 ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: failed to send device information.", gattc_if, event);
        //                 return;
        //             }
        //         }
        //     }
        // }
        break;
    }
    case ESP_GATTC_SCAN_FLT_CFG_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] SCAN_FLT_CFG_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
    {
        if (ESP_GATT_OK != param->reg_for_notify.status)
        {
            ESP_LOGE(LOG_TAG, "[%u] REG_FOR_NOTIFY_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->reg_for_notify.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tHandle: %d", gattc_if, event, param->reg_for_notify.handle);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] REG_FOR_NOTIFY_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->reg_for_notify.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tCharacteristic Handle: %d", gattc_if, event, param->reg_for_notify.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Fetch the descriptor
        esp_gatt_status_t status;
        esp_gattc_descr_elem_t descr_elem_result;
        uint16_t descr_count = 1;
        ESP_LOGI(LOG_TAG, "[%u][%d] fetching descriptor...", gattc_if, event);
        status = esp_ble_gattc_get_descr_by_char_handle(gattc_if,
                                                        gl_profile_tab[PROFILE_OMRON_ID].conn_id,
                                                        param->reg_for_notify.handle,
                                                        notify_descr_uuid,
                                                        &descr_elem_result,
                                                        &descr_count);
        if (ESP_GATT_OK != status) {
            ESP_LOGE(LOG_TAG, "[%u][%d] failed to fetch the descriptor, error code = 0x%x", gattc_if, event, status);
            break;
        }

        // Write to the descriptor
        if (ESP_UUID_LEN_16 == descr_elem_result.uuid.len && ESP_GATT_UUID_CHAR_CLIENT_CONFIG == descr_elem_result.uuid.uuid.uuid16)
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] enable the notification on descriptor %d...", gattc_if, event, descr_elem_result.handle);
            status = esp_ble_gattc_write_char_descr(gattc_if,
                                                    gl_profile_tab[PROFILE_OMRON_ID].conn_id,
                                                    descr_elem_result.handle,
                                                    sizeof(CCCD_INDICATION_ENABLED),
                                                    (uint8_t *)&CCCD_INDICATION_ENABLED,
                                                    ESP_GATT_WRITE_TYPE_RSP,
                                                    ESP_GATT_AUTH_REQ_NONE);
            if (ESP_GATT_OK != status) {
                ESP_LOGE(LOG_TAG, "[%u][%d] failed to enable the notification, error code = 0x%x", gattc_if, event, status);
                break;
            }
        }

        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] NOTIFY_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->notify.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", gattc_if, event, param->notify.remote_bda[0], param->notify.remote_bda[1], param->notify.remote_bda[2], param->notify.remote_bda[3], param->notify.remote_bda[4], param->notify.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->notify.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tValue (%u):", gattc_if, event, param->notify.value_len);
        esp_log_buffer_hex(LOG_TAG, param->notify.value, param->notify.value_len);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tIs Notify: %u", gattc_if, event, param->notify.is_notify);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->notify.is_notify)
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] ESP_GATTC_NOTIFY_EVT, received NOTIFY value:", gattc_if, event);
        }
        else
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] ESP_GATTC_NOTIFY_EVT, received INDICATE value:", gattc_if, event);
            // esp_ble_gattc_send_response(gattc_if, param->notify.conn_id, param->notify.handle, ESP_GATT_OK, NULL);
        }
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:
    {
        if (ESP_GATT_OK != param->write.status) {
            ESP_LOGE(LOG_TAG, "[%u] WRITE_DESCR_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->write.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->write.conn_id);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->write.handle);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tOffset (%u):", gattc_if, event, param->write.offset);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] WRITE_DESCR_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->write.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->write.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->write.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tOffset (%u):", gattc_if, event, param->write.offset);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        ESP_LOGI(LOG_TAG, "[%u][%d] reading descriptor %d on connection %u on interface %u...", gattc_if, event, param->write.handle, param->write.conn_id, gattc_if);
        esp_err_t err = esp_ble_gattc_read_char_descr(gattc_if, param->search_cmpl.conn_id, param->write.handle, ESP_GATT_AUTH_REQ_NONE);
        if (ESP_OK != err)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] failed to read descriptor, error code = 0x%x", gattc_if, event, err);
        }
        // uint8_t write_char_data[35];
        // for (int i = 0; i < sizeof(write_char_data); ++i)
        // {
        //     write_char_data[i] = i % 256;
        // }
        // esp_ble_gattc_write_char(gattc_if,
        //                          gl_profile_tab[PROFILE_OMRON_ID].conn_id,
        //                          param->write.handle,
        //                          sizeof(write_char_data),
        //                          write_char_data,
        //                          ESP_GATT_WRITE_TYPE_RSP,
        //                          ESP_GATT_AUTH_REQ_NONE);
        break;
    }
    case ESP_GATTC_SRVC_CHG_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] SRVC_CHG_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", gattc_if, event, param->srvc_chg.remote_bda[0], param->srvc_chg.remote_bda[1], param->srvc_chg.remote_bda[2], param->srvc_chg.remote_bda[3], param->srvc_chg.remote_bda[4], param->srvc_chg.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:
    {
        if (ESP_GATT_OK != param->write.status) {
            ESP_LOGE(LOG_TAG, "[%u] WRITE_CHAR_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->write.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->write.conn_id);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->write.handle);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tOffset (%u):", gattc_if, event, param->write.offset);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] WRITE_CHAR_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->write.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->write.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->write.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tOffset (%u):", gattc_if, event, param->write.offset);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        ESP_LOGI(LOG_TAG, "[%u][%d] write char success ", gattc_if, event);
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] DISCONNECT_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tReason: %d", gattc_if, event, param->disconnect.reason);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->disconnect.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", gattc_if, event, param->disconnect.remote_bda[0], param->disconnect.remote_bda[1], param->disconnect.remote_bda[2], param->disconnect.remote_bda[3], param->disconnect.remote_bda[4], param->disconnect.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        // Update connection state
        fetch_characteristics = false;
        break;
    }
    case ESP_GATTC_CLOSE_EVT:
    {
        if (ESP_GATT_OK != param->close.status) {
            ESP_LOGE(LOG_TAG, "[%u] CLOSE_EVT [%d]", gattc_if, event);
            ESP_LOGE(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->close.status);
            ESP_LOGE(LOG_TAG, "[%u][%d]", gattc_if, event);
            break;
        }

        ESP_LOGI(LOG_TAG, "[%u] CLOSE_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->close.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->close.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: %02X:%02X:%02X:%02X:%02X:%02X", gattc_if, event, param->close.remote_bda[0], param->close.remote_bda[1], param->close.remote_bda[2], param->close.remote_bda[3], param->close.remote_bda[4], param->close.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tReason: %d", gattc_if, event, param->close.reason);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    }
    default:
        ESP_LOGW(LOG_TAG, "[%u] UNHANDLED EVENT [%d]", gattc_if, event);
        ESP_LOGW(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    }
    ESP_LOGI(LOG_TAG, "");
}

void esp_gattc_intercept_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    bool swallow_event = false;
    /* If event is register event, store the gattc_if for each profile */
    switch (event)
    {
    case ESP_GATTC_REG_EVT:
    {
        ESP_LOGD(LOG_TAG, "[INTERCEPT] intercepted REG_EVT [%u] on interface %u", event, gattc_if);
        if (param->reg.status == ESP_GATT_OK)
        {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
            ESP_LOGD(LOG_TAG, "[INTERCEPT] updated application (0x%04x) state, with interface %u", param->reg.app_id, gattc_if);
        }
        else
        {
            ESP_LOGE(LOG_TAG, "[INTERCEPT] reg app failed, app_id 0x%04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
        break;
    }
    case ESP_GATTC_SEARCH_RES_EVT:
    {
        ESP_LOGD(LOG_TAG, "[INTERCEPT] intercepted SEARCH_RES_EVT [%u] on interface %u", event, gattc_if);

        // Search list for valid service id or existing node with matching UUID
        bool store_service = false;
        if ((ESP_UUID_LEN_16 == param->search_res.srvc_id.uuid.len && ESP_GATT_UUID_BLOOD_PRESSURE_SVC == param->search_res.srvc_id.uuid.uuid.uuid16)
         || (ESP_UUID_LEN_16 == param->search_res.srvc_id.uuid.len && ESP_GATT_UUID_BATTERY_SERVICE_SVC == param->search_res.srvc_id.uuid.uuid.uuid16)
         || (ESP_UUID_LEN_16 == param->search_res.srvc_id.uuid.len && ESP_GATT_UUID_CURRENT_TIME_SVC == param->search_res.srvc_id.uuid.uuid.uuid16)
        ) {
            store_service = true;
            for (esp_gattc_service_elem_node_t * n = service_elem_list ; n ; n = n->next) {
                if (n->element.uuid.len == param->search_res.srvc_id.uuid.len
                && memcmp(&n->element.uuid.uuid, &param->search_res.srvc_id.uuid.uuid, n->element.uuid.len) == 0) {
                    store_service = false;
                    ESP_LOGW(LOG_TAG, "[INTERCEPT] found service in list");
                    break;
                }
            }
        } else {
            switch (param->search_res.srvc_id.uuid.len)
            {
            case ESP_UUID_LEN_16:
                ESP_LOGW(LOG_TAG, "[INTERCEPT] ignoring event for service with UUID 0x%04X", param->search_res.srvc_id.uuid.uuid.uuid16);
                break;
            case ESP_UUID_LEN_32:
                ESP_LOGW(LOG_TAG, "[INTERCEPT] ignoring service with UUID 0x%08lX", param->search_res.srvc_id.uuid.uuid.uuid32);
                break;
            case ESP_UUID_LEN_128:
                ESP_LOGW(LOG_TAG, "[INTERCEPT] ignoring service with UUID %08lX-%04X-%04X-%04X-%012llX", *(uint32_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[12], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[10], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[8], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[6], *(uint64_t *)param->search_res.srvc_id.uuid.uuid.uuid128 & 0x0000FFFFFFFFFFFF);
                break;
            };
            swallow_event = true;
        }

        // If service not found in list, add it
        if (store_service) {
            switch (param->search_res.srvc_id.uuid.len)
            {
            case ESP_UUID_LEN_16:
                ESP_LOGD(LOG_TAG, "[INTERCEPT] add service (0x%04X) to list...", param->search_res.srvc_id.uuid.uuid.uuid16);
                break;
            case ESP_UUID_LEN_32:
                ESP_LOGD(LOG_TAG, "[INTERCEPT] add service (0x%08lX) to list...", param->search_res.srvc_id.uuid.uuid.uuid32);
                break;
            case ESP_UUID_LEN_128:
                ESP_LOGD(LOG_TAG, "[INTERCEPT] add service (%08lX-%04X-%04X-%04X-%012llX) to list...", *(uint32_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[12], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[10], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[8], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[6], *(uint64_t *)param->search_res.srvc_id.uuid.uuid.uuid128 & 0x0000FFFFFFFFFFFF);
                break;
            };
            esp_gattc_service_elem_node_t * new_service = (esp_gattc_service_elem_node_t *)malloc(sizeof(esp_gattc_service_elem_node_t));
            memset(new_service, 0, sizeof(esp_gattc_service_elem_node_t));
            new_service->element.is_primary = param->search_res.is_primary;
            new_service->element.start_handle = param->search_res.start_handle;
            new_service->element.end_handle = param->search_res.end_handle;
            new_service->element.uuid.len = param->search_res.srvc_id.uuid.len;
            memcpy(&new_service->element.uuid.uuid, &param->search_res.srvc_id.uuid.uuid, param->search_res.srvc_id.uuid.len);

            // Push onto the front of the service list
            new_service->next = service_elem_list;
            service_elem_list = new_service;
            ESP_LOGD(LOG_TAG, "[INTERCEPT] added service to list");
        }

        break;
    }
    default:
        break;
    };

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    if (swallow_event) {
        ESP_LOGW(LOG_TAG, "[INTERCEPT] swallowed event %u on interface %u", event, gattc_if);
    } else for (int idx = 0; idx < PROFILE_COUNT; idx++) {
        if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            gattc_if == gl_profile_tab[idx].gattc_if)
        {
            if (gl_profile_tab[idx].gattc_cb)
            {
                gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
            }
        }
    }
}
