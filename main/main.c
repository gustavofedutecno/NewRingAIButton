#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "nvs_flash.h"

#include "mic.h"
#include "speaker.h"

#define SPP_TAG "MAIN"
#define SPP_SERVER_NAME "SPP_SERVER"

typedef enum
{
    MODE_WAITING,
    MODE_MIC,
    MODE_SPEAKER
} system_mode_t;

static system_mode_t current_mode = MODE_WAITING;
static uint32_t client_handle = 0;
static bool spp_ready = false;

void set_mode(system_mode_t new_mode)
{

    if (current_mode == new_mode)
        return;

    switch (new_mode)
    {
    case MODE_MIC:
        ESP_LOGI(SPP_TAG, "Modo micrófono activado");
        speaker_stop(); 
        mic_start(); 
        break;

    case MODE_SPEAKER:
        ESP_LOGI(SPP_TAG, "case MODE_SPEAKER");
        mic_stop();
        ESP_LOGI(SPP_TAG, "Microfono detenido");
        speaker_start();
        ESP_LOGI(SPP_TAG, "Modo parlante listo");
        break;

    default:
        break;
    }

    current_mode = new_mode;
}

// ===================== CALLBACKS REGISTRADOS =========================

void mic_finished_callback()
{
    set_mode(MODE_SPEAKER);
}

void speaker_idle_callback()
{
    if (spp_ready)
    {
        set_mode(MODE_MIC);
    }
}

// ===================== CALLBACK SPP =========================

void spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event)
    {
    case ESP_SPP_INIT_EVT:
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, SPP_SERVER_NAME);
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        client_handle = param->srv_open.handle;
        mic_set_client_handle(param->srv_open.handle);
        spp_ready = true;

        set_mode(MODE_MIC); // Se establece el modo MIC por defecto
        break;

    case ESP_SPP_START_EVT:
    ESP_LOGI(SPP_TAG, "Servidor Iniciado");
        break;

    case ESP_SPP_WRITE_EVT:
    case ESP_SPP_CONG_EVT:
        if (current_mode == MODE_MIC)
        {
            mic_spp_callback(event, param);
        }
        break;

    case ESP_SPP_DATA_IND_EVT:
        if (current_mode == MODE_SPEAKER)
        {
            speaker_spp_callback(event, param);
        }
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(SPP_TAG, "Client disconnected");
        spp_ready = false;
        client_handle = 0;
        mic_stop();       
        set_mode(MODE_WAITING);
        break;

    default:
        ESP_LOGI(SPP_TAG, "Unhandled SPP event: %d", event);
        break;
    }
}

void app_main(void)
{
    // Inicialización de NVS
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

    mic_register_mode_switch_callback(mic_finished_callback);
    speaker_register_mode_switch_callback(speaker_idle_callback);

}
