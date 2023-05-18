#include "omron_gatt_dis.h"

// Include C standard libraries
#include <string.h>

// Include ESP-IDF libraries
#include <esp_gap_ble_api.h>
#include <esp_log.h>

// Include Notecard note-c library
#include <note.h>

#include "omron_gap_scan.h"
#include "omron_profile_id.h"

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
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event)
    {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(LOG_TAG, "REG_EVT");
        break;
    case ESP_GATTC_SCAN_FLT_CFG_EVT:
        ESP_LOGI(LOG_TAG, "SCAN_FLT_CFG_EVT");
        break;
    case ESP_GATTC_CONNECT_EVT:
    {
        ESP_LOGI(LOG_TAG, "ESP_GATTC_CONNECT_EVT conn_id %u, if %d", p_data->connect.conn_id, gattc_if);
        gl_profile_tab[PROFILE_DIS_ID].conn_id = p_data->connect.conn_id;
        memcpy(gl_profile_tab[PROFILE_DIS_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(LOG_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(LOG_TAG, gl_profile_tab[PROFILE_DIS_ID].remote_bda, sizeof(esp_bd_addr_t));
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req(gattc_if, p_data->connect.conn_id);
        if (mtu_ret)
        {
            ESP_LOGE(LOG_TAG, "config MTU error, error code = 0x%x", mtu_ret);
        }
        break;
    }
    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "open failed, status %d", p_data->open.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "open success");
        break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        if (param->dis_srvc_cmpl.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "discover service failed, status %d", param->dis_srvc_cmpl.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "discover service complete conn_id %u", param->dis_srvc_cmpl.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "config mtu failed, error status = 0x%x", param->cfg_mtu.status);
        }
        ESP_LOGI(LOG_TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %u", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
    {
        ESP_LOGI(LOG_TAG, "SEARCH RES: conn_id = %u is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(LOG_TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == ESP_GATT_UUID_DEVICE_INFO_SVC)
        {
            ESP_LOGI(LOG_TAG, "service found");
            get_server = true;
            gl_profile_tab[PROFILE_DIS_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_DIS_ID].service_end_handle = p_data->search_res.end_handle;
            ESP_LOGI(LOG_TAG, "UUID16: 0x%X", p_data->search_res.srvc_id.uuid.uuid.uuid16);
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p_data->search_cmpl.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "search service failed, error status = 0x%x", p_data->search_cmpl.status);
            break;
        }
        if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE)
        {
            ESP_LOGI(LOG_TAG, "Get service information from remote device");
        }
        else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH)
        {
            ESP_LOGI(LOG_TAG, "Get service information from flash");
        }
        else
        {
            ESP_LOGI(LOG_TAG, "unknown service source");
        }
        ESP_LOGI(LOG_TAG, "ESP_GATTC_SEARCH_CMPL_EVT");
        if (get_server)
        {
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                    p_data->search_cmpl.conn_id,
                                                                    ESP_GATT_DB_CHARACTERISTIC,
                                                                    gl_profile_tab[PROFILE_DIS_ID].service_start_handle,
                                                                    gl_profile_tab[PROFILE_DIS_ID].service_end_handle,
                                                                    EMPTY_HANDLE,
                                                                    &count);
            if (status != ESP_GATT_OK)
            {
                ESP_LOGE(LOG_TAG, "esp_ble_gattc_get_attr_count error");
            }

            if (count > 0)
            {
                char_result = (zak_gattc_char_t *)malloc(sizeof(zak_gattc_char_t) * count);
                if (!char_result)
                {
                    ESP_LOGE(LOG_TAG, "gattc no mem");
                }
                else
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        // Initialize the char_result struct
                        memset((void *)&char_result[i], 0, sizeof(zak_gattc_char_t));
                        uint16_t char_count = count;
                        status = esp_ble_gattc_get_char_by_uuid(gattc_if,
                                                                p_data->search_cmpl.conn_id,
                                                                gl_profile_tab[PROFILE_DIS_ID].service_start_handle,
                                                                gl_profile_tab[PROFILE_DIS_ID].service_end_handle,
                                                                dis_characteristics[i],
                                                                &char_result[i].element,
                                                                &char_count);
                        if (status != ESP_GATT_OK)
                        {
                            ESP_LOGE(LOG_TAG, "esp_ble_gattc_get_char_by_uuid error");
                            continue;
                        }
                        ESP_LOGI(LOG_TAG, "esp_ble_gattc_get_char_by_uuid found %u characteristic(s)", char_count);

                        // Interrogate the properties of the characteristic
                        if (ESP_GATT_CHAR_PROP_BIT_NOTIFY & char_result[i].element.properties)
                        {
                            gl_profile_tab[PROFILE_DIS_ID].char_handle = char_result[i].element.char_handle;
                            esp_err_t err = esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_DIS_ID].remote_bda, char_result[i].element.char_handle);
                            if (ESP_OK != err)
                            {
                                ESP_LOGE(LOG_TAG, "Failed to register for notification for: 0x%X (error: %d).", char_result[i].element.uuid.uuid.uuid16, err);
                            }
                        }
                        if (ESP_GATT_CHAR_PROP_BIT_READ & char_result[i].element.properties)
                        {
                            gl_profile_tab[PROFILE_DIS_ID].char_handle = char_result[i].element.char_handle;
                            ESP_LOGI(LOG_TAG, "Read characteristic 0x%X from profile %u on connection %u with handle: %d.", dis_characteristics[i].uuid.uuid16, PROFILE_DIS_ID, gl_profile_tab[PROFILE_DIS_ID].conn_id, char_result[i].element.char_handle);
                            esp_err_t err = esp_ble_gattc_read_char(gattc_if, gl_profile_tab[PROFILE_DIS_ID].conn_id, char_result[i].element.char_handle, ESP_GATT_AUTH_REQ_NONE);
                            if (ESP_OK != err)
                            {
                                ESP_LOGE(LOG_TAG, "Failed to read characteristic: 0x%X (error: %d).", char_result[i].element.uuid.uuid.uuid16, err);

                                // Mark as loaded so we don't wait for the read char event
                                char_result[i].value_loaded = true;
                            }
                        }
                    }
                }
            }
            else
            {
                ESP_LOGE(LOG_TAG, "no char found");
            }
        }
        break;
    case ESP_GATTC_READ_CHAR_EVT:
    {
        ESP_LOGI(LOG_TAG, "ESP_GATTC_READ_CHAR_EVT");
        if (p_data->read.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "READ CHAR failed: error status = %d", p_data->read.status);
        }
        else
        {
            size_t load_count = 0;
            for (size_t i = 0; i < DIS_CHAR_COUNT; ++i) {
                if (!char_result[i].value_loaded && p_data->read.handle == char_result[i].element.char_handle) {
                    // Load the value into the struct
                    char_result[i].value_len = p_data->read.value_len;
                    char_result[i].value = (uint8_t *)malloc(sizeof(uint8_t) * char_result[i].value_len + sizeof(uint8_t));
                    char_result[i].value = memcpy(char_result[i].value, p_data->read.value, char_result[i].value_len);
                    char_result[i].value[char_result[i].value_len] = '\0';
                    char_result[i].value_loaded = true;

                    // Add to the load count
                    ++load_count;
                    ESP_LOGI(LOG_TAG, "Characteristic value saved for 0x%X", char_result[i].element.uuid.uuid.uuid16);
                } else if (char_result[i].value_loaded) {
                    // Already loaded, count in the load count
                    ++load_count;
                }
            }

            if (load_count == DIS_CHAR_COUNT) {
                load_count = 0;
                ESP_LOGI(LOG_TAG, "All characteristic values loaded, and ready for transmission.");

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
                        ESP_LOGI("NOTE-C", "Bluetooth Device Address: %s", note_buffer);
                        snprintf(uuid16_buffer, sizeof(uuid16_buffer), "0x%04X", ESP_GATT_UUID_DEVICE_INFO_SVC);
                        JAddStringToObject(body, "GATT Service", uuid16_buffer);
                        ESP_LOGI("NOTE-C", "GATT Service: %s", uuid16_buffer);
                        for (size_t i = 0; i < DIS_CHAR_COUNT; ++i) {
                            ESP_LOGI(LOG_TAG, "Characteristic (0x%X) data on Handle %d, Len %d, Value:", char_result[i].element.uuid.uuid.uuid16, char_result[i].element.char_handle, char_result[i].value_len);
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
                                ESP_LOGI(LOG_TAG, "%s", char_result[i].value);
                                strncpy(note_buffer, (const char *)char_result[i].value, sizeof(note_buffer));
                            }
                            JAddStringToObject(body, uuid16_buffer, note_buffer);
                            ESP_LOGI("NOTE-C", "%s: %s", uuid16_buffer, note_buffer);
                        }
                        if (!NoteRequest(req))
                        {
                            NoteDebug("Failed to send device information.");
                            return;
                        }
                    }
                }
            }
        }
        break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
    {
        ESP_LOGI(LOG_TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT");
        if (p_data->reg_for_notify.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
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
                ESP_LOGE(LOG_TAG, "esp_ble_gattc_get_attr_count error");
            }
            if (count > 0)
            {
                descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
                if (!descr_elem_result)
                {
                    ESP_LOGE(LOG_TAG, "malloc error, gattc no mem");
                }
                else
                {
                    ret_status = esp_ble_gattc_get_descr_by_char_handle(gattc_if,
                                                                        gl_profile_tab[PROFILE_DIS_ID].conn_id,
                                                                        p_data->reg_for_notify.handle,
                                                                        notify_descr_uuid,
                                                                        descr_elem_result,
                                                                        &count);
                    if (ret_status != ESP_GATT_OK)
                    {
                        ESP_LOGE(LOG_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
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
                        ESP_LOGE(LOG_TAG, "esp_ble_gattc_write_char_descr error");
                    }

                    /* free descr_elem_result */
                    free(descr_elem_result);
                }
            }
            else
            {
                ESP_LOGE(LOG_TAG, "decsr not found");
            }
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        if (p_data->notify.is_notify)
        {
            ESP_LOGI(LOG_TAG, "ESP_GATTC_NOTIFY_EVT, receive notify value:");
        }
        else
        {
            ESP_LOGI(LOG_TAG, "ESP_GATTC_NOTIFY_EVT, receive indicate value:");
        }
        esp_log_buffer_hex(LOG_TAG, p_data->notify.value, p_data->notify.value_len);
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p_data->write.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "write descr failed, error status = 0x%x", p_data->write.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "write descr success ");
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
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(LOG_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
        esp_log_buffer_hex(LOG_TAG, bda, sizeof(esp_bd_addr_t));
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (p_data->write.status != ESP_GATT_OK)
        {
            ESP_LOGE(LOG_TAG, "write char failed, error status = 0x%x", p_data->write.status);
            break;
        }
        ESP_LOGI(LOG_TAG, "write char success ");
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        connect = false;
        get_server = false;
        ESP_LOGI(LOG_TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d", p_data->disconnect.reason);
        break;
    default:
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
