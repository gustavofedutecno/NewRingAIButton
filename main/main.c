#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "nvs_flash.h"

// Archivos header de los periféricos
#include "mic.h"
#include "speaker.h"

// Definición de las etiquetas para debugging
#define SPP_TAG "MAIN"
#define SPP_SERVER_NAME "SPP_SERVER"

// Definición de los estados disponibles del sistema
typedef enum
{
    MODE_WAITING,
    MODE_MIC,
    MODE_SPEAKER
} system_mode_t;

static system_mode_t current_mode = MODE_WAITING;  // Modo inicial
static uint32_t client_handle = 0;
static bool spp_ready = false;  //Flag para el envío de datos

void set_mode(system_mode_t new_mode)
{

    // No es necesario cambiar de modo al mismo que el anterior
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
        ESP_LOGI(SPP_TAG, "Modo parlante activado");
        mic_stop();
        speaker_start();
        break;

    default:
        break;
    }

    // Se actualiza el nuevo modo
    current_mode = new_mode;
}


//Callback para cambiar modo a parlante
void mic_finished_callback()
{
    set_mode(MODE_SPEAKER);
}

//Callback para cambiar al modo micrófono
void speaker_idle_callback()
{
    if (spp_ready)
    {
        set_mode(MODE_MIC);
    }
}

// Callbacks del servidor SPP
void spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event)
    {
    case ESP_SPP_INIT_EVT:
        //Creación del servidor SPP
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, SPP_SERVER_NAME);
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        //Se abre la conexión entre el servidor y el cliente 
        client_handle = param->srv_open.handle;
        mic_set_client_handle(param->srv_open.handle);
        spp_ready = true;

        set_mode(MODE_MIC); // Se establece el modo MIC por defecto
        break;

    case ESP_SPP_START_EVT:
        ESP_LOGI(SPP_TAG, "Servidor Iniciado");
        break;

    case ESP_SPP_CONG_EVT:
        //Caso de cogestión en modo micrófono
        if (current_mode == MODE_MIC)
        {
            mic_spp_callback(event, param);
        }

        if (current_mode == MODE_SPEAKER)
        {
            ESP_LOGI(SPP_TAG,"Congestión en el parlante");
        }
        
        break;

    case ESP_SPP_DATA_IND_EVT:
        //Recepción de datos sólamente en modo parlante
        if (current_mode == MODE_SPEAKER)
        {
            speaker_spp_callback(event, param);
        }
        break;

    case ESP_SPP_CLOSE_EVT:
        //Desconexión del cliente al servidor
        ESP_LOGI(SPP_TAG, "Client disconnected");
        spp_ready = false;
        client_handle = 0;
        mic_stop();       
        set_mode(MODE_WAITING);
        break;

    case ESP_SPP_WRITE_EVT:
        //ESP_LOGI(SPP_TAG,"Evento escritura desde el server");
        break;

    default:
        //Caso desconocido 
        ESP_LOGI(SPP_TAG, "Unhandled SPP event: %d", event);
        break;
    }
}

void app_main(void)
{
    //Liberación de memoria del NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    //Inicialización del stack de Bluetooth
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    //Inicialización del servidor SPP y sus características
    ESP_ERROR_CHECK(esp_spp_register_callback(spp_callback));
    ESP_ERROR_CHECK(esp_spp_init(ESP_SPP_MODE_CB));

    esp_bt_gap_set_device_name("ESP32_RING_AI");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    //Callbacks del micrófono y parlante
    mic_register_mode_switch_callback(mic_finished_callback);
    speaker_register_mode_switch_callback(speaker_idle_callback);

}
