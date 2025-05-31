#include "mic.h"
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spp_api.h"

#define SPP_TAG "MIC"
#define I2S_PORT I2S_NUM_0
#define I2S_WS GPIO_NUM_25
#define I2S_SCK GPIO_NUM_26
#define I2S_SD GPIO_NUM_27
#define BUFFER_LEN 512
#define SAMPLE_RATE 8000
#define REAL_BYTES_X_SAMPLE 4
#define CIRCULAR_BUFFER_SIZE 65536
#define BUTTON_GPIO GPIO_NUM_34
#define LED_GPIO GPIO_NUM_32

extern void mic_to_speaker_switch();

static int ganancia = 13;
static int congestiones = 0;

static uint8_t circular_buffer[CIRCULAR_BUFFER_SIZE];
static volatile size_t write_index = 0;
static volatile size_t read_index = 0;
static volatile size_t data_available = 0;

static uint32_t spp_client_handle = 0;
volatile bool spp_can_send = true;
volatile bool recording = false;

static uint8_t pending_buffer[1024];
static size_t pending_len = 0;

static TaskHandle_t i2s_task_handle = NULL;
static TaskHandle_t button_task_handle = NULL;
static TaskHandle_t sender_task_handle = NULL;

void i2s_config_init()
{
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_LEN,
        .use_apll = true};

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD};

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) return;

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) return;

    i2s_zero_dma_buffer(I2S_PORT);
}

void circular_buffer_write(uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        if (data_available < CIRCULAR_BUFFER_SIZE)
        {
            circular_buffer[write_index] = data[i];
            write_index = (write_index + 1) % CIRCULAR_BUFFER_SIZE;
            data_available++;
        }
    }
}

size_t circular_buffer_read(uint8_t *data, size_t length)
{
    size_t bytes_read = 0;
    while (bytes_read < length && data_available > 0)
    {
        data[bytes_read] = circular_buffer[read_index];
        read_index = (read_index + 1) % CIRCULAR_BUFFER_SIZE;
        data_available--;
        bytes_read++;
    }
    return bytes_read;
}

void bluetooth_sender_task(void *arg)
{
    while (1)
    {
        if (gpio_get_level(BUTTON_GPIO) == 1 &&
            spp_can_send &&
            spp_client_handle != 0 &&
            pending_len == 0)
        {
            size_t bytes = circular_buffer_read(pending_buffer, sizeof(pending_buffer));
            if (bytes > 0)
            {
                pending_len = bytes;
                spp_can_send = false;
                esp_err_t err = esp_spp_write(spp_client_handle, pending_len, pending_buffer);
                if (err != ESP_OK)
                {
                    spp_can_send = true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void i2s_reader_task(void *arg)
{
    uint8_t temp_buffer[REAL_BYTES_X_SAMPLE * BUFFER_LEN];
    int16_t processed_samples[BUFFER_LEN];
    size_t bytes_read;

    while (1)
    {
        esp_err_t err = i2s_read(I2S_PORT, temp_buffer, sizeof(temp_buffer), &bytes_read, portMAX_DELAY);
        if (err == ESP_OK && bytes_read > 0)
        {
            size_t samples_read = bytes_read / 4;
            int32_t *samples_32bit = (int32_t *)temp_buffer;
            for (size_t i = 0; i < samples_read; i++)
            {
                processed_samples[i] = samples_32bit[i] >> ganancia;
            }

            if (recording)
            {
                circular_buffer_write((uint8_t *)processed_samples, samples_read * sizeof(int16_t));
            }
        }
    }
}

void button_task(void *arg)
{
    bool last_state = false;
    int stable_counter = 0;
    const int debounce_ticks = 5;

    while (1)
    {
        bool current_state = gpio_get_level(BUTTON_GPIO);

        if (current_state == last_state)
        {
            if (stable_counter < debounce_ticks)
            {
                stable_counter++;
            }
            else if (stable_counter == debounce_ticks)
            {
                if (current_state && !recording)
                {
                    gpio_set_level(LED_GPIO, 1);
                    recording = true;
                }
                else if (!current_state && recording)
                {
                    gpio_set_level(LED_GPIO, 0);
                    recording = false;
                    mic_to_speaker_switch();
                    vTaskDelete(NULL);
                }
                stable_counter++;
            }
        }
        else
        {
            stable_counter = 0;
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void mic_start_tasks()
{
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    i2s_config_init();

    xTaskCreate(button_task, "button_task", 8192, NULL, 5, &button_task_handle);
    xTaskCreate(i2s_reader_task, "i2s_reader_task", 8192, NULL, 6, &i2s_task_handle);
    xTaskCreate(bluetooth_sender_task, "bluetooth_sender_task", 4096, NULL, 6, &sender_task_handle);
}

void mic_stop_tasks()
{
    if (i2s_task_handle) {
        vTaskDelete(i2s_task_handle);
        i2s_task_handle = NULL;
    }
    if (button_task_handle) {
        vTaskDelete(button_task_handle);
        button_task_handle = NULL;
    }
    if (sender_task_handle) {
        vTaskDelete(sender_task_handle);
        sender_task_handle = NULL;
    }

    i2s_driver_uninstall(I2S_PORT);
    recording = false;
}

void mic_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event)
    {
    case ESP_SPP_WRITE_EVT:
        spp_can_send = true;
        pending_len = 0;

        if (!recording)
        {
            write_index = 0;
            read_index = 0;
            data_available = 0;
        }
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        spp_client_handle = param->srv_open.handle;
        break;

    case ESP_SPP_CONG_EVT:
        spp_can_send = !param->cong.cong;
        if (param->cong.cong == 1 && recording)
        {
            congestiones += 1;
        }
        break;

    default:
        break;
    }
}
