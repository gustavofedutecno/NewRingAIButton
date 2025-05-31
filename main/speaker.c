#include "speaker.h"
#include "esp_log.h"
#include "driver/dac_oneshot.h"
#include "esp_timer.h"
#include "esp_spp_api.h"

#define SAMPLE_RATE 8000
#define TIMER_INTERVAL_US (1000000 / SAMPLE_RATE)
#define AUDIO_BUFFER_SIZE 8192
#define MIN_BUFFER_TO_START 4096
#define DEVICE_NAME "ESP32_AUDIO_DAC"
#define TAG "SPEAKER"

extern void speaker_to_mic_switch(); // Declaración para callback

static dac_oneshot_handle_t dac_handle;
static esp_timer_handle_t audio_timer;

static uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static bool playback_started = false;

static inline bool buffer_is_empty()
{
    return buffer_head == buffer_tail;
}

static inline bool buffer_is_full()
{
    return ((buffer_head + 1) % AUDIO_BUFFER_SIZE) == buffer_tail;
}

static void buffer_write(uint8_t data)
{
    if (!buffer_is_full())
    {
        audio_buffer[buffer_head] = data;
        buffer_head = (buffer_head + 1) % AUDIO_BUFFER_SIZE;
    }
}

static bool buffer_read(uint8_t *data)
{
    if (!buffer_is_empty())
    {
        *data = audio_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % AUDIO_BUFFER_SIZE;
        return true;
    }
    return false;
}

static int buffer_occupancy()
{
    if (buffer_head >= buffer_tail)
        return buffer_head - buffer_tail;
    else
        return AUDIO_BUFFER_SIZE - buffer_tail + buffer_head;
}

static void audio_timer_callback(void *arg)
{
    uint8_t sample;

    if (!playback_started)
    {
        if (buffer_occupancy() >= MIN_BUFFER_TO_START)
        {
            playback_started = true;
        }
        else
        {
            dac_oneshot_output_voltage(dac_handle, 0x80);  // Silencio
            return;
        }
    }

    if (buffer_read(&sample))
    {
        dac_oneshot_output_voltage(dac_handle, sample);
    }
    else
    {
        playback_started = false;
        dac_oneshot_output_voltage(dac_handle, 0x80); // Silencio

        // Si no hay datos por cierto tiempo, volvemos a modo micrófono
        speaker_to_mic_switch();
    }
}

void speaker_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (event == ESP_SPP_DATA_IND_EVT)
    {
        for (int i = 0; i < param->data_ind.len; i++)
        {
            buffer_write(param->data_ind.data[i]);
        }
    }
}

void speaker_start()
{
    // Inicializar DAC
    dac_oneshot_config_t dac_cfg = {
        .chan_id = DAC_CHAN_0,
    };
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&dac_cfg, &dac_handle));

    // Inicializar timer
    const esp_timer_create_args_t timer_args = {
        .callback = &audio_timer_callback,
        .name = "audio_timer"
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &audio_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(audio_timer, TIMER_INTERVAL_US));
}

void speaker_stop()
{
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
}
