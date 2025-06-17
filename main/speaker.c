#include "speaker.h"
#include "esp_log.h"
#include "driver/dac_oneshot.h"
#include "esp_timer.h"
#include "esp_spp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//Parámetros del parlante
#define SAMPLE_RATE 8000
#define TIMER_INTERVAL_US (1000000 / SAMPLE_RATE)
#define AUDIO_BUFFER_SIZE 8192
#define MIN_BUFFER_TO_START 4096

#define TAG "SPEAKER"

static dac_oneshot_handle_t dac_handle = NULL;
static esp_timer_handle_t audio_timer = NULL;

//Parámetros del buffer de reproducción
static uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static bool playback_started = false;

static TaskHandle_t speaker_task_handle = NULL;
static bool speaker_active = false;

static speaker_event_cb_t mode_switch_callback = NULL;

void speaker_register_mode_switch_callback(speaker_event_cb_t cb)
{
    mode_switch_callback = cb;
}

static inline bool buffer_is_empty() {
    return buffer_head == buffer_tail;
}

static inline bool buffer_is_full() {
    return ((buffer_head + 1) % AUDIO_BUFFER_SIZE) == buffer_tail;
}

static void buffer_write(uint8_t data) {
    if (!buffer_is_full()) {
        audio_buffer[buffer_head] = data;
        buffer_head = (buffer_head + 1) % AUDIO_BUFFER_SIZE;
    }
}

static bool buffer_read(uint8_t *data) {
    if (!buffer_is_empty()) {
        *data = audio_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % AUDIO_BUFFER_SIZE;
        return true;
    }
    return false;
}

static int buffer_occupancy() {
    if (buffer_head >= buffer_tail)
        return buffer_head - buffer_tail;
    else
        return AUDIO_BUFFER_SIZE - buffer_tail + buffer_head;
}

//Callback del timer de audio
static void audio_timer_callback(void *arg)
{
    uint8_t sample;

    if (!playback_started) {
        if (buffer_occupancy() >= MIN_BUFFER_TO_START) {
            //Se activa la reproducción si el buffer está minimamente lleno
            playback_started = true;
        } else {
            dac_oneshot_output_voltage(dac_handle, 0x80); //Silencio
            return;
        }
    }

    //Si la lectura es no vacía
    if (buffer_read(&sample)) {
        //Se envía el valor del sample al DAC
        dac_oneshot_output_voltage(dac_handle, sample);
    } else {
        //Se envía silencio
        playback_started = false;
        dac_oneshot_output_voltage(dac_handle, 0x80);

        if (mode_switch_callback) {
            mode_switch_callback();
        }
    }
}

void speaker_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (event == ESP_SPP_DATA_IND_EVT) {
        //Se escriben los datos leídos desde el servidor hasta el buffer
        for (int i = 0; i < param->data_ind.len; i++) {
            buffer_write(param->data_ind.data[i]);
        }
    }
}

void speaker_start()
{
    ESP_LOGI(TAG, "Iniciando el parlante");

    if (speaker_active) return;

    //Se define el puerto de salida del DAC (pin 25)
    dac_oneshot_config_t dac_cfg = {
        .chan_id = DAC_CHAN_0, 
    };
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&dac_cfg, &dac_handle));

    const esp_timer_create_args_t timer_args = {
        .callback = &audio_timer_callback,
        .name = "audio_timer"
    };

    //Se inicia el timer cada 125[ms] para la reproducción de audio en 8[kHz]
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &audio_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(audio_timer, TIMER_INTERVAL_US));

    speaker_active = true;
    ESP_LOGI(TAG, "Parlante iniciado");
}

void speaker_stop()
{
    ESP_LOGI(TAG, "Iniciando detención del parlante");

    if (!speaker_active) {
        ESP_LOGI(TAG, "Parlante no activo y detenido");
        return;
    }

    //Se elimina el timer y el periférico DAC
    if (audio_timer) {
        esp_timer_stop(audio_timer);
        esp_timer_delete(audio_timer);
        audio_timer = NULL;
    }

    if (dac_handle) {
        dac_oneshot_del_channel(dac_handle);
        dac_handle = NULL;
    }

    playback_started = false;
    buffer_head = 0;
    buffer_tail = 0;

    speaker_active = false;
    ESP_LOGI(TAG, "Parlante detenido");
}
