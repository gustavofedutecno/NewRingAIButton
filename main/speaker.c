#include "speaker.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "esp_spp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

#define TAG "SPEAKER"

#define SAMPLE_RATE 4000
#define I2S_DMA_BUF_LEN 1024
#define I2S_DMA_BUF_COUNT 16
#define AUDIO_FRAME_SIZE 256  
#define AUDIO_QUEUE_SIZE 60 

static speaker_event_cb_t mode_switch_callback = NULL;
static bool speaker_active = false;
static QueueHandle_t audio_queue = NULL;
static TaskHandle_t speaker_task_handle = NULL;

void speaker_register_mode_switch_callback(speaker_event_cb_t cb) {
    mode_switch_callback = cb;
}

static void i2s_dac_init() {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB, //posible cambio de formato
        .intr_alloc_flags = 0,
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0};

    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN));
    i2s_zero_dma_buffer(I2S_NUM_0);
}

static void i2s_dac_deinit() {
    i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
    i2s_driver_uninstall(I2S_NUM_0);
}

static void speaker_i2s_task(void *param) {
    uint8_t raw_data[AUDIO_FRAME_SIZE];
    uint16_t converted[AUDIO_FRAME_SIZE];
    size_t bytes_written;

    while (1) {
        if (xQueueReceive(audio_queue, raw_data, portMAX_DELAY)) {
            for (int i = 0; i < AUDIO_FRAME_SIZE; i++) {
                converted[i] = ((int16_t)raw_data[i] - 128) << 8; //Conversión a int16
            }

            i2s_write(I2S_NUM_0, converted, sizeof(converted), &bytes_written, portMAX_DELAY);

            UBaseType_t count = uxQueueMessagesWaiting(audio_queue);
            ESP_LOGI(TAG, "Elementos en la cola de audio: %u", count);

        }
    }
}

void speaker_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    if (event == ESP_SPP_DATA_IND_EVT && speaker_active) {
        int len = param->data_ind.len;
        const uint8_t *data = param->data_ind.data;

        while (len > 0) {
            int block_size = (len >= AUDIO_FRAME_SIZE) ? AUDIO_FRAME_SIZE : len;
            uint8_t buffer[AUDIO_FRAME_SIZE];

            // Asegura que TODO el buffer esté limpio
            memset(buffer, 128, AUDIO_FRAME_SIZE);  // Relleno neutro para audio 8-bit unsigned

            // Copia solo lo recibido, el resto se mantiene en 128 (silencio)
            memcpy(buffer, data, block_size);

            if (audio_queue != NULL) {
                if (xQueueSend(audio_queue, buffer, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Cola de audio llena, se pierde paquete");
                }
            }

            data += block_size;
            len -= block_size;
        }
    }
}


void speaker_start() {
    if (speaker_active) {
        ESP_LOGI(TAG, "Parlante ya iniciado");
        return;
    }

    ESP_LOGI(TAG, "Iniciando parlante...");

    // Crear cola si no existe
    if (audio_queue == NULL) {
        audio_queue = xQueueCreate(AUDIO_QUEUE_SIZE, AUDIO_FRAME_SIZE);
        if (audio_queue == NULL) {
            ESP_LOGE(TAG, "Fallo al crear la cola de audio");
            return;
        }
    }

    // Inicializar I2S y el DAC
    i2s_dac_init();

    // Crear tarea del I2S
    xTaskCreate(speaker_i2s_task, "speaker_i2s_task", 4096, NULL, 7, &speaker_task_handle);

    speaker_active = true;
    ESP_LOGI(TAG, "Parlante en ejecución");
}

void speaker_stop() {
    if (!speaker_active)
        return;

    ESP_LOGI(TAG, "Deteniendo parlante");

    // Eliminar la tarea
    if (speaker_task_handle != NULL) {
        vTaskDelete(speaker_task_handle);
        speaker_task_handle = NULL;
    }

    // Detener y liberar I2S
    i2s_dac_deinit();

    // Borrar la cola
    if (audio_queue != NULL) {
        vQueueDelete(audio_queue);
        audio_queue = NULL;
    }

    speaker_active = false;
}