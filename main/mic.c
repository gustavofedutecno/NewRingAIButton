#include "mic.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "esp_spp_api.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"

#define SPP_TAG "MIC"

#define I2S_PORT I2S_NUM_0
#define I2S_WS GPIO_NUM_2
#define I2S_SCK GPIO_NUM_0
#define I2S_SD GPIO_NUM_15
#define BUFFER_LEN 512
#define SAMPLE_RATE 8000
#define REAL_BYTES_X_SAMPLE 4
#define CIRCULAR_BUFFER_SIZE 65536
#define BUTTON_GPIO GPIO_NUM_26
#define LED_GPIO GPIO_NUM_27

static uint8_t circular_buffer[CIRCULAR_BUFFER_SIZE];
static volatile size_t write_index = 0;
static volatile size_t read_index = 0;
static volatile size_t data_available = 0;

static uint32_t spp_client_handle = 0;
static volatile bool spp_can_send = true;
static volatile bool recording = false;
static volatile bool micflag = false;
static volatile bool i2s_running = false;

static uint8_t pending_buffer[1024];
static size_t pending_len = 0;

static TaskHandle_t i2s_task_handle = NULL;
static TaskHandle_t button_task_handle = NULL;
static TaskHandle_t sender_task_handle = NULL;
static TaskHandle_t mic_manager_task_handle = NULL;

static mic_event_cb_t mode_switch_callback = NULL;

// NUEVO: estado del driver I2S
static bool i2s_initialized = false;

void mic_register_mode_switch_callback(mic_event_cb_t cb)
{
    mode_switch_callback = cb;
}

void mic_set_recording(bool enable)
{
    recording = enable;
    gpio_set_level(LED_GPIO, enable ? 1 : 0);
}

void mic_set_client_handle(uint32_t handle)
{
    spp_client_handle = handle;
}

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

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pin_config));
    i2s_zero_dma_buffer(I2S_PORT);
    i2s_initialized = true;
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
        if (recording && spp_can_send && spp_client_handle != 0 && pending_len == 0)
        {
            size_t bytes = circular_buffer_read(pending_buffer, sizeof(pending_buffer));
            if (bytes > 0)
            {
                pending_len = bytes;
                spp_can_send = false;
                esp_err_t err = esp_spp_write(spp_client_handle, pending_len, pending_buffer);
                if (err == ESP_OK)
                {
                    spp_can_send = true;
                    pending_len = 0;
                }
                else
                {
                    ESP_LOGW(SPP_TAG, "Error en escritura: %s", esp_err_to_name(err));
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
    int ganancia = 13;
    i2s_running = true;

    while (i2s_running)
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

    vTaskDelete(NULL);
}

void mic_manager_task(void *arg)
{
    while (1)
    {
        if (micflag)
        {
            if (!recording)
            {
                mic_set_recording(true);
            }
            else
            {
                mic_set_recording(false);
                if (mode_switch_callback)
                {
                    mode_switch_callback();
                }
            }
            micflag = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void button_task(void *arg)
{
    bool last_state = false;

    while (1)
    {
        bool current_state = gpio_get_level(BUTTON_GPIO);
        if (current_state != last_state)
        {
            micflag = true;
            last_state = current_state;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void mic_start()
{
    ESP_LOGI(SPP_TAG, "Reanudando o iniciando tareas del micrófono");
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    mic_set_recording(false);

    if (!i2s_initialized)
    {
        i2s_config_init();
    }

    if (button_task_handle == NULL)
        xTaskCreate(button_task, "button_task", 4096, NULL, 5, &button_task_handle);
    else
        vTaskResume(button_task_handle);

    if (i2s_task_handle == NULL)
        xTaskCreate(i2s_reader_task, "i2s_reader_task", 8192, NULL, 6, &i2s_task_handle);
    else
        vTaskResume(i2s_task_handle);

    if (sender_task_handle == NULL)
        xTaskCreate(bluetooth_sender_task, "bluetooth_sender_task", 4096, NULL, 6, &sender_task_handle);
    else
        vTaskResume(sender_task_handle);

    if (mic_manager_task_handle == NULL)
        xTaskCreate(mic_manager_task, "mic_manager_task", 4096, NULL, 6, &mic_manager_task_handle);
    else
        vTaskResume(mic_manager_task_handle);

    ESP_LOGI(SPP_TAG, "Micrófono en ejecución");
}

void mic_stop()
{
    ESP_LOGI(SPP_TAG, "Iniciando detención de micrófono");
    i2s_running = false;

    if (i2s_task_handle) {
        int timeout = 10;
        while (eTaskGetState(i2s_task_handle) != eDeleted && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        i2s_task_handle = NULL;
    }

    if (i2s_task_handle)
        vTaskSuspend(i2s_task_handle);
    if (button_task_handle)
        vTaskSuspend(button_task_handle);
    if (sender_task_handle)
        vTaskSuspend(sender_task_handle);

    mic_set_recording(false);
    spp_can_send = true;
    pending_len = 0;
    write_index = 0;
    read_index = 0;
    data_available = 0;

    ESP_LOGI(SPP_TAG, "Detención del micrófono finalizada");
}

// Función para consultar si el I2S está instalado (visible desde speaker.c)
bool mic_is_i2s_installed(void)
{
    return i2s_initialized;
}

// Función para desinstalar el I2S desde speaker.c si se requiere
void mic_uninstall_i2s(void)
{
    if (i2s_initialized) {
        ESP_LOGI(SPP_TAG, "Desinstalando I2S desde mic.c...");
        i2s_driver_uninstall(I2S_PORT);
        i2s_initialized = false;
    }
}

// Callbacks del SPP para el micrófono
void mic_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event)
    {
    case ESP_SPP_SRV_OPEN_EVT:
        spp_client_handle = param->srv_open.handle;
        break;

    case ESP_SPP_CONG_EVT:
        ESP_LOGI(SPP_TAG, "Servidor congestionado");
        spp_can_send = !param->cong.cong;
        break;

    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(SPP_TAG, "SPP client initialized");
        break;

    default:
        break;
    }
}

