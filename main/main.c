#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "nvs_flash.h"
#include "esp_mac.h"

#include "mic.h"
#include "speaker.h"

#define SPP_TAG "MAIN"
#define SPP_SERVER_NAME "SPP_SERVER"

typedef enum {
    MODE_WAITING,
    MODE_MIC,
    MODE_SPEAKER
} system_mode_t;

static system_mode_t current_mode = MODE_WAITING;
static uint32_t client_handle = 0;
static bool spp_ready = false;

// Router general del SPP
void spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event)
    {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(SPP_TAG, "SPP init, starting service");
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, SPP_SERVER_NAME);
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "Client connected");
        client_handle = param->srv_open.handle;
        spp_ready = true;
        current_mode = MODE_MIC;
        mic_start_tasks();
        break;

    case ESP_SPP_WRITE_EVT:
    case ESP_SPP_CONG_EVT:
        if (current_mode == MODE_MIC) {
            mic_spp_callback(event, param);
        }
        break;

    case ESP_SPP_DATA_IND_EVT:
        if (current_mode == MODE_SPEAKER) {
            speaker_spp_callback(event, param);
        }
        break;

    default:
        ESP_LOGI(SPP_TAG, "Unhandled SPP event: %d", event);
        break;
    }
}

// Función llamada cuando se detecta fin de grabación
void mic_to_speaker_switch()
{
    mic_stop_tasks();
    current_mode = MODE_SPEAKER;
    speaker_start();
    ESP_LOGI(SPP_TAG, "Switched to SPEAKER mode");
}

// Función llamada por el speaker cuando detecta silencio prolongado
void speaker_to_mic_switch()
{
    if (spp_ready) {
        current_mode = MODE_MIC;
        mic_start_tasks();
        ESP_LOGI(SPP_TAG, "Switched back to MIC mode");
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_spp_register_callback(spp_callback));
    ESP_ERROR_CHECK(esp_spp_init(ESP_SPP_MODE_CB));

    esp_bt_gap_set_device_name("ESP32_RING_AI");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(SPP_TAG, "Device initialized, waiting for connection...");
}
