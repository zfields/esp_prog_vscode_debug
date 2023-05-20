#include "omron_gatt_dis.h"

// Include C standard libraries
#include <string.h>

// Include ESP-IDF libraries
#include <esp_gap_ble_api.h>
#include <esp_log.h>

// Include Notecard note-c library
#include <note.h>

#include "omron_gap_scan.h"
#include "omron_defs.h"

#define LOG_TAG "GATTC_DIS"

typedef struct {
    esp_gattc_char_elem_t element;
    uint16_t value_len;
    uint8_t *value;
    bool value_loaded;
} zak_gattc_char_t;

struct gattc_profile_inst gl_profile_tab[PROFILE_COUNT] = {
    [PROFILE_DIS_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    }
};

static zak_gattc_char_t *char_result = NULL;
static esp_gattc_descr_elem_t *descr_elem_result = NULL;

static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_DEVICE_INFO_SVC}
};

static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}
};

#define DIS_CHAR_COUNT 8
static esp_bt_uuid_t dis_characteristics[DIS_CHAR_COUNT] = {
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_SYSTEM_ID},
    },
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_MODEL_NUMBER_STR},
    },
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_SERIAL_NUMBER_STR},
    },
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_FW_VERSION_STR},
    },
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_HW_VERSION_STR},
    },
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_SW_VERSION_STR},
    },
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_MANU_NAME},
    },
    {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = ESP_GATT_UUID_IEEE_DATA},
    }
};

volatile static bool get_server = false;

void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    ESP_LOGI(LOG_TAG, "");
    ESP_LOGI(LOG_TAG, "Interface Type: %u", gattc_if);
    switch (event)
    {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(LOG_TAG, "[%u] REG_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->reg.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tApplication ID: %u", gattc_if, event, param->reg.app_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    case ESP_GATTC_CONNECT_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] CONNECT_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d] Connection ID: %u", gattc_if, event, param->connect.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d] Link Role: %s", gattc_if, event, (param->connect.conn_id ? "Slave" : "Master"));
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X", gattc_if, event, param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2], param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection Interval: %u", gattc_if, event, param->connect.conn_params.interval);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection Latency: %u", gattc_if, event, param->connect.conn_params.latency);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection Timeout: %u", gattc_if, event, param->connect.conn_params.timeout);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        gl_profile_tab[PROFILE_DIS_ID].conn_id = param->connect.conn_id;
        memcpy(gl_profile_tab[PROFILE_DIS_ID].remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req(gattc_if, param->connect.conn_id);
        if (mtu_ret)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] config MTU error, error code = 0x%x", gattc_if, event, mtu_ret);
        }
        break;
    }
    case ESP_GATTC_OPEN_EVT:
        ESP_LOGI(LOG_TAG, "[%u] OPEN_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->open.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->open.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X", gattc_if, event, param->open.remote_bda[0], param->open.remote_bda[1], param->open.remote_bda[2], param->open.remote_bda[3], param->open.remote_bda[4], param->open.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tMTU Size: %u", gattc_if, event, param->open.mtu);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->open.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] open failed, status %d", gattc_if, event, param->open.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "[%u][%d] open success", gattc_if, event);
        break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        ESP_LOGI(LOG_TAG, "[%u] DIS_SRVC_CMPL_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->dis_srvc_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->dis_srvc_cmpl.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->dis_srvc_cmpl.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] discover service failed, status %d", gattc_if, event, param->dis_srvc_cmpl.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "[%u][%d] discover service complete conn_id %u", gattc_if, event, param->dis_srvc_cmpl.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] SEARCH_RES_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->search_res.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStart Handle: %u", gattc_if, event, param->search_res.start_handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tEnd Handle: %u", gattc_if, event, param->search_res.end_handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tService ID UUID Length: %u", gattc_if, event, param->search_res.srvc_id.uuid.len);
        switch (param->search_res.srvc_id.uuid.len)
        {
        case ESP_UUID_LEN_16:
            ESP_LOGI(LOG_TAG, "[%u][%d]\tService ID UUID: 0x%04X", gattc_if, event, param->search_res.srvc_id.uuid.uuid.uuid16);
            break;
        case ESP_UUID_LEN_32:
            ESP_LOGI(LOG_TAG, "[%u][%d]\tService ID UUID: 0x%08lX", gattc_if, event, param->search_res.srvc_id.uuid.uuid.uuid32);
            break;
        case ESP_UUID_LEN_128:
            ESP_LOGI(LOG_TAG, "[%u][%d]\tService ID UUID: %08lX-%04X-%04X-%04X-%12llX", gattc_if, event, *(uint32_t *)param->search_res.srvc_id.uuid.uuid.uuid128, *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[4], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[6], *(uint16_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[8], *(uint64_t *)&param->search_res.srvc_id.uuid.uuid.uuid128[10]);
            break;
        };
        ESP_LOGI(LOG_TAG, "[%u][%d]\tService ID Instance: %u", gattc_if, event, param->search_res.srvc_id.inst_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tIs Primary Service: %u", gattc_if, event, param->search_res.is_primary);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && param->search_res.srvc_id.uuid.uuid.uuid16 == ESP_GATT_UUID_DEVICE_INFO_SVC)
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] service found", gattc_if, event);
            get_server = true;
            gl_profile_tab[PROFILE_DIS_ID].service_start_handle = param->search_res.start_handle;
            gl_profile_tab[PROFILE_DIS_ID].service_end_handle = param->search_res.end_handle;
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        ESP_LOGI(LOG_TAG, "[%u] SEARCH_CMPL_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->search_cmpl.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->search_cmpl.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tSearched Service Source: %u", gattc_if, event, param->search_cmpl.searched_service_source);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->search_cmpl.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] search service failed, error status = 0x%x", gattc_if, event, param->search_cmpl.status);
            break;
        }
        if (param->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE)
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] Get service information from remote device", gattc_if, event);
        }
        else if (param->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH)
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] Get service information from flash", gattc_if, event);
        }
        else
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] unknown service source", gattc_if, event);
        }
        if (get_server)
        {
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                    param->search_cmpl.conn_id,
                                                                    ESP_GATT_DB_CHARACTERISTIC,
                                                                    gl_profile_tab[PROFILE_DIS_ID].service_start_handle,
                                                                    gl_profile_tab[PROFILE_DIS_ID].service_end_handle,
                                                                    EMPTY_HANDLE,
                                                                    &count);
            if (status != ESP_GATT_OK)
            {
                ESP_LOGE(LOG_TAG, "[%u][%d] esp_ble_gattc_get_attr_count error", gattc_if, event);
            }

            if (count > 0)
            {
                char_result = (zak_gattc_char_t *)malloc(sizeof(zak_gattc_char_t) * count);
                if (!char_result)
                {
                    ESP_LOGE(LOG_TAG, "[%u][%d] gattc no mem", gattc_if, event);
                }
                else
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        // Initialize the char_result struct
                        memset((void *)&char_result[i], 0, sizeof(zak_gattc_char_t));
                        uint16_t char_count = count;
                        status = esp_ble_gattc_get_char_by_uuid(gattc_if,
                                                                param->search_cmpl.conn_id,
                                                                gl_profile_tab[PROFILE_DIS_ID].service_start_handle,
                                                                gl_profile_tab[PROFILE_DIS_ID].service_end_handle,
                                                                dis_characteristics[i],
                                                                &char_result[i].element,
                                                                &char_count);
                        if (status != ESP_GATT_OK)
                        {
                            ESP_LOGE(LOG_TAG, "[%u][%d] esp_ble_gattc_get_char_by_uuid error", gattc_if, event);
                            continue;
                        }
                        ESP_LOGI(LOG_TAG, "[%u][%d] esp_ble_gattc_get_char_by_uuid found %u characteristic(s)", gattc_if, event, char_count);

                        // Interrogate the properties of the characteristic
                        if (ESP_GATT_CHAR_PROP_BIT_NOTIFY & char_result[i].element.properties)
                        {
                            gl_profile_tab[PROFILE_DIS_ID].char_handle = char_result[i].element.char_handle;
                            esp_err_t err = esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_DIS_ID].remote_bda, char_result[i].element.char_handle);
                            if (ESP_OK != err)
                            {
                                ESP_LOGE(LOG_TAG, "[%u][%d] failed to register for notification for: 0x%X (error: %d).", gattc_if, event, char_result[i].element.uuid.uuid.uuid16, err);
                            }
                        }
                        if (ESP_GATT_CHAR_PROP_BIT_READ & char_result[i].element.properties)
                        {
                            gl_profile_tab[PROFILE_DIS_ID].char_handle = char_result[i].element.char_handle;
                            ESP_LOGI(LOG_TAG, "[%u][%d] read characteristic 0x%X from profile %u on connection %u with handle: %d.", gattc_if, event, dis_characteristics[i].uuid.uuid16, PROFILE_DIS_ID, gl_profile_tab[PROFILE_DIS_ID].conn_id, char_result[i].element.char_handle);
                            esp_err_t err = esp_ble_gattc_read_char(gattc_if, gl_profile_tab[PROFILE_DIS_ID].conn_id, char_result[i].element.char_handle, ESP_GATT_AUTH_REQ_NONE);
                            if (ESP_OK != err)
                            {
                                ESP_LOGE(LOG_TAG, "[%u][%d] failed to read characteristic: 0x%X (error: %d).", gattc_if, event, char_result[i].element.uuid.uuid.uuid16, err);

                                // Mark as loaded so we don't wait for the read char event
                                char_result[i].value_loaded = true;
                            }
                        }
                    }
                }
            }
            else
            {
                ESP_LOGE(LOG_TAG, "[%u][%d] no char found", gattc_if, event);
            }
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        ESP_LOGI(LOG_TAG, "[%u] CFG_MTU_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->cfg_mtu.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->cfg_mtu.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tMTU Size: %u", gattc_if, event, param->cfg_mtu.mtu);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->cfg_mtu.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] config MTU failed, error status = 0x%x", gattc_if, event, param->cfg_mtu.status);
        }
        ESP_LOGI(LOG_TAG, "[%u][%d] config MTU succeeded, MTU %d, conn_id %u", gattc_if, event, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        break;
    case ESP_GATTC_READ_CHAR_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] READ_CHAR_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->read.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->read.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->read.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tValue (%u):", gattc_if, event, param->read.value_len);
        esp_log_buffer_hex(LOG_TAG, param->read.value, param->read.value_len);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->read.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] failed to read characteristic: error status = %d", gattc_if, event, param->read.status);
        }
        else
        {
            size_t load_count = 0;
            for (size_t i = 0; i < DIS_CHAR_COUNT; ++i) {
                if (!char_result[i].value_loaded && param->read.handle == char_result[i].element.char_handle) {
                    // Load the value into the struct
                    char_result[i].value_len = param->read.value_len;
                    char_result[i].value = (uint8_t *)malloc(sizeof(uint8_t) * char_result[i].value_len + sizeof(uint8_t));
                    char_result[i].value = memcpy(char_result[i].value, param->read.value, char_result[i].value_len);
                    char_result[i].value[char_result[i].value_len] = '\0';
                    char_result[i].value_loaded = true;

                    // Add to the load count
                    ++load_count;
                    ESP_LOGI(LOG_TAG, "[%u][%d] characteristic value saved for 0x%X", gattc_if, event, char_result[i].element.uuid.uuid.uuid16);
                } else if (char_result[i].value_loaded) {
                    // Already loaded, count in the load count
                    ++load_count;
                }
            }

            if (load_count == DIS_CHAR_COUNT) {
                load_count = 0;
                ESP_LOGI(LOG_TAG, "[%u][%d] all characteristic values loaded, and ready for transmission.", gattc_if, event);

                // Send a Note to Notehub
                J *req;
                if ((req = NoteNewRequest("note.add")))
                {
                    JAddStringToObject(req, "file", "omron.qo");
                    JAddBoolToObject(req, "sync", true);
                    J *body = JAddObjectToObject(req, "body");
                    if (body) {
                        char uuid16_buffer[8];
                        char note_buffer[32];
                        snprintf(note_buffer, sizeof(note_buffer), "%02X:%02X:%02X:%02X:%02X:%02X", gl_profile_tab[PROFILE_DIS_ID].remote_bda[0], gl_profile_tab[PROFILE_DIS_ID].remote_bda[1], gl_profile_tab[PROFILE_DIS_ID].remote_bda[2], gl_profile_tab[PROFILE_DIS_ID].remote_bda[3], gl_profile_tab[PROFILE_DIS_ID].remote_bda[4], gl_profile_tab[PROFILE_DIS_ID].remote_bda[5]);
                        JAddStringToObject(body, "Bluetooth Device Address", note_buffer);
                        ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: Bluetooth Device Address: %s", gattc_if, event, note_buffer);
                        snprintf(uuid16_buffer, sizeof(uuid16_buffer), "0x%04X", ESP_GATT_UUID_DEVICE_INFO_SVC);
                        JAddStringToObject(body, "GATT Service", uuid16_buffer);
                        ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: GATT Service: %s", gattc_if, event, uuid16_buffer);
                        for (size_t i = 0; i < DIS_CHAR_COUNT; ++i) {
                            ESP_LOGI(LOG_TAG, "[%u][%d] Characteristic (0x%X) data on Handle %d, Len %d, Value:", gattc_if, event, char_result[i].element.uuid.uuid.uuid16, char_result[i].element.char_handle, char_result[i].value_len);
                            snprintf(uuid16_buffer, sizeof(uuid16_buffer), "0x%04X", char_result[i].element.uuid.uuid.uuid16);
                            if (ESP_GATT_UUID_SYSTEM_ID == char_result[i].element.uuid.uuid.uuid16
                             || ESP_GATT_UUID_IEEE_DATA == char_result[i].element.uuid.uuid.uuid16) {
                                esp_log_buffer_hex(LOG_TAG, char_result[i].value, char_result[i].value_len);
                                note_buffer[0] = '0';
                                note_buffer[1] = 'x';
                                note_buffer[2] = '\0';
                                for (size_t j = 0; j < char_result[i].value_len; ++j) {
                                    sniprintf(note_buffer, sizeof(note_buffer), "%s%02X", note_buffer, char_result[i].value[j]);
                                }
                            } else {
                                ESP_LOGI(LOG_TAG, "[%u][%d] %s", gattc_if, event, char_result[i].value);
                                strncpy(note_buffer, (const char *)char_result[i].value, sizeof(note_buffer));
                            }
                            JAddStringToObject(body, uuid16_buffer, note_buffer);
                            ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: %s: %s", gattc_if, event, uuid16_buffer, note_buffer);
                        }
                        if (!NoteRequest(req))
                        {
                            ESP_LOGI(LOG_TAG, "[%u][%d] NoteAdd: failed to send device information.", gattc_if, event);
                            return;
                        }
                    }
                }
            }
        }
        break;
    }
    case ESP_GATTC_SCAN_FLT_CFG_EVT:
        ESP_LOGI(LOG_TAG, "[%u] SCAN_FLT_CFG_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] REG_FOR_NOTIFY_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->reg_for_notify.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %d", gattc_if, event, param->reg_for_notify.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->reg_for_notify.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] REG FOR NOTIFY failed: error status = %d", gattc_if, event, param->reg_for_notify.status);
        }
        else
        {
            uint16_t count = 0;
            uint16_t notify_en = 1;
            esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                        gl_profile_tab[PROFILE_DIS_ID].conn_id,
                                                                        ESP_GATT_DB_DESCRIPTOR,
                                                                        gl_profile_tab[PROFILE_DIS_ID].service_start_handle,
                                                                        gl_profile_tab[PROFILE_DIS_ID].service_end_handle,
                                                                        gl_profile_tab[PROFILE_DIS_ID].char_handle,
                                                                        &count);
            if (ret_status != ESP_GATT_OK)
            {
                ESP_LOGE(LOG_TAG, "[%u][%d] esp_ble_gattc_get_attr_count error", gattc_if, event);
            }
            if (count > 0)
            {
                descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
                if (!descr_elem_result)
                {
                    ESP_LOGE(LOG_TAG, "[%u][%d] malloc error, gattc no mem", gattc_if, event);
                }
                else
                {
                    ret_status = esp_ble_gattc_get_descr_by_char_handle(gattc_if,
                                                                        gl_profile_tab[PROFILE_DIS_ID].conn_id,
                                                                        param->reg_for_notify.handle,
                                                                        notify_descr_uuid,
                                                                        descr_elem_result,
                                                                        &count);
                    if (ret_status != ESP_GATT_OK)
                    {
                        ESP_LOGE(LOG_TAG, "[%u][%d] esp_ble_gattc_get_descr_by_char_handle error", gattc_if, event);
                    }
                    /* Every char has only one descriptor in our 'ESP_GATTS_DEMO' demo, so we used first 'descr_elem_result' */
                    if (count > 0 && descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG)
                    {
                        ret_status = esp_ble_gattc_write_char_descr(gattc_if,
                                                                    gl_profile_tab[PROFILE_DIS_ID].conn_id,
                                                                    descr_elem_result[0].handle,
                                                                    sizeof(notify_en),
                                                                    (uint8_t *)&notify_en,
                                                                    ESP_GATT_WRITE_TYPE_RSP,
                                                                    ESP_GATT_AUTH_REQ_NONE);
                    }

                    if (ret_status != ESP_GATT_OK)
                    {
                        ESP_LOGE(LOG_TAG, "[%u][%d] esp_ble_gattc_write_char_descr error", gattc_if, event);
                    }

                    /* free descr_elem_result */
                    free(descr_elem_result);
                }
            }
            else
            {
                ESP_LOGE(LOG_TAG, "[%u][%d] decsr not found", gattc_if, event);
            }
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(LOG_TAG, "[%u] NOTIFY_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->notify.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X", gattc_if, event, param->notify.remote_bda[0], param->notify.remote_bda[1], param->notify.remote_bda[2], param->notify.remote_bda[3], param->notify.remote_bda[4], param->notify.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->notify.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tValue (%u):", gattc_if, event, param->notify.value_len);
        esp_log_buffer_hex(LOG_TAG, param->notify.value, param->notify.value_len);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tIs Notify: %u", gattc_if, event, param->notify.is_notify);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->notify.is_notify)
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] ESP_GATTC_NOTIFY_EVT, receive notify value:", gattc_if, event);
        }
        else
        {
            ESP_LOGI(LOG_TAG, "[%u][%d] ESP_GATTC_NOTIFY_EVT, receive indicate value:", gattc_if, event);
        }
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        ESP_LOGI(LOG_TAG, "[%u] WRITE_DESCR_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->write.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->write.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->write.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tOffset (%u):", gattc_if, event, param->write.offset);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->write.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "[%u][%d] write descr failed, error status = 0x%x", gattc_if, event, param->write.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "[%u][%d] write descr success", gattc_if, event);
        uint8_t write_char_data[35];
        for (int i = 0; i < sizeof(write_char_data); ++i)
        {
            write_char_data[i] = i % 256;
        }
        esp_ble_gattc_write_char(gattc_if,
                                 gl_profile_tab[PROFILE_DIS_ID].conn_id,
                                 gl_profile_tab[PROFILE_DIS_ID].char_handle,
                                 sizeof(write_char_data),
                                 write_char_data,
                                 ESP_GATT_WRITE_TYPE_RSP,
                                 ESP_GATT_AUTH_REQ_NONE);
        break;
    case ESP_GATTC_SRVC_CHG_EVT:
    {
        ESP_LOGI(LOG_TAG, "[%u] SRVC_CHG_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X", gattc_if, event, param->srvc_chg.remote_bda[0], param->srvc_chg.remote_bda[1], param->srvc_chg.remote_bda[2], param->srvc_chg.remote_bda[3], param->srvc_chg.remote_bda[4], param->srvc_chg.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:
        ESP_LOGI(LOG_TAG, "[%u] WRITE_CHAR_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->write.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->write.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tHandle: %u", gattc_if, event, param->write.handle);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tOffset (%u):", gattc_if, event, param->write.offset);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        if (param->write.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "write char failed, error status = 0x%x", param->write.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "write char success ");
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(LOG_TAG, "[%u] DISCONNECT_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tReason: %d", gattc_if, event, param->disconnect.reason);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->disconnect.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X", gattc_if, event, param->disconnect.remote_bda[0], param->disconnect.remote_bda[1], param->disconnect.remote_bda[2], param->disconnect.remote_bda[3], param->disconnect.remote_bda[4], param->disconnect.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);

        connect = false;
        get_server = false;
        break;
    case ESP_GATTC_CLOSE_EVT:
        ESP_LOGI(LOG_TAG, "[%u] CLOSE_EVT [%d]", gattc_if, event);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tStatus: %d", gattc_if, event, param->close.status);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tConnection ID: %u", gattc_if, event, param->close.conn_id);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tRemote Bluetooth Device Address: 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X", gattc_if, event, param->close.remote_bda[0], param->close.remote_bda[1], param->close.remote_bda[2], param->close.remote_bda[3], param->close.remote_bda[4], param->close.remote_bda[5]);
        ESP_LOGI(LOG_TAG, "[%u][%d]\tReason: %d", gattc_if, event, param->close.reason);
        ESP_LOGI(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    default:
        ESP_LOGW(LOG_TAG, "[%u] UNHANDLED EVENT [%d]", gattc_if, event);
        ESP_LOGW(LOG_TAG, "[%u][%d]", gattc_if, event);
        break;
    }
}

void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        }
        else
        {
            ESP_LOGI(LOG_TAG, "reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do
    {
        int idx;
        for (idx = 0; idx < PROFILE_COUNT; idx++)
        {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gattc_if == gl_profile_tab[idx].gattc_if)
            {
                if (gl_profile_tab[idx].gattc_cb)
                {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}
