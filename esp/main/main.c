#include <stdio.h>
#include <stdlib.h>
#include <esp_mac.h>
#include <math.h>
#include "driver/rmt_tx.h"
#include "led_ws2812b.h"
#include "LD2410C.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/semphr.h"

#define NUM_SAMPLES 128
#define MAX_BRIGHTNESS 255

QueueHandle_t detection_queue;
QueueHandle_t button_intr;
SemaphoreHandle_t is_led_on_mutex;
bool is_led_on = false;
static uint8_t color_value = 1;

const char *TAG = "ESP";
typedef struct
{
    rmt_channel_handle_t led_chan;
    rmt_encoder_handle_t led_encoder;
    rmt_transmit_config_t tx_config;
} led_config_t;

static inline void change_led_color(uint8_t R_value, uint8_t G_value, uint8_t B_value, uint8_t *led_data)
{
    for (int i = 0; i < 8; i++)
    {
        led_data[i * 3 + 0] = G_value;
        led_data[i * 3 + 1] = R_value;
        led_data[i * 3 + 2] = B_value;
    }
}

void led_control(void *arg)
{
    uint8_t detection_state;
    led_config_t *led_config = (led_config_t *)arg;
    // khai báo led data
    uint8_t led_data[3 * 8];
    while (1)
    {
        if (xQueueReceive(detection_queue, &detection_state, portMAX_DELAY)) // block cho tới khi nhận tín hiệu
        {
            xSemaphoreTake(is_led_on_mutex, portMAX_DELAY);
            switch (detection_state)
            {
            case NO_HUMAN:
                // ESP_LOGI(TAG, "Off led");
                is_led_on = false;
                change_led_color(0, 0, 0, led_data);
                break;
            case REDUCE_STATE:
                // bat led mau cam
                is_led_on = false;
                change_led_color(2, 1, 0, led_data);
                break;
            default:
                // ESP_LOGI(TAG, "On led");
                is_led_on = true;
                change_led_color(color_value, color_value, color_value, led_data);
                break;
            }
            ESP_ERROR_CHECK(rmt_transmit(led_config->led_chan, led_config->led_encoder, led_data, sizeof(led_data), &led_config->tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_config->led_chan, portMAX_DELAY));
            xSemaphoreGive(is_led_on_mutex);
        }
        // UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI("Read stack size", "High water mark: %d", stack_high_water_mark); // check số word còn lại trong stack
    }
}

void timer_init(void)
{
    handle_config_t *handle_config = malloc(sizeof(handle_config_t));
    xTaskCreate(noise_reduce, "noise_reduce_task", 2048, handle_config, 10, &handle_config->noise_task_handle);
    esp_timer_create_args_t args_timer = {
        .callback = stop_engineering_mode,
        .name = "Timer for stop noise reduce",
        .arg = handle_config};
    esp_timer_create(&args_timer, &handle_config->timer_handle);
}

void task_button_intr(void *arg)
{
    while (1)
    {
        bool request = false;
        uint8_t detection_state = REDUCE_STATE;
        // chờ tín hiệu interrupt
        if (xQueueReceive(button_intr, &request, portMAX_DELAY) == pdPASS)
        {
            if (request)
            {
                gpio_intr_disable(GPIO_NUM_13);
                xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
                timer_init(); // Gọi hàm khởi tạo timer và chạy reduce
            }
        }
        // UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI("Read stack size", "High water mark: %d", stack_high_water_mark); // check số word còn lại trong stack
    }
}

void IRAM_ATTR io_intr_handler(void *arg)
{
    bool request = true;
    xQueueSendFromISR(button_intr, &request, NULL); // gửi tín hiệu interrupt
}

int compare(const void *a, const void *b) {
    return (*(int *)a - *(int *)b); // Trả về giá trị âm, dương hoặc 0 để sắp xếp
}

void adjust_brightness(void *arg)
{
    adc_oneshot_unit_handle_t *adc1_handle = (adc_oneshot_unit_handle_t *)arg;
    uint8_t detection_state = 2;
    uint8_t current_value = 0;
    uint8_t old_value = 0;
    while (1)
    {
        int values[NUM_SAMPLES];
        for (int i = 0; i < NUM_SAMPLES; i++)
        {
            adc_oneshot_read(*adc1_handle, ADC_CHANNEL_5, &values[i]);
        }
        // Sắp xếp mảng
        qsort(values, NUM_SAMPLES, sizeof(int), compare);

        // Loại bỏ phần tử lớn nhất và nhỏ nhất
        int sum = 0;
        for (int i = 1; i < NUM_SAMPLES - 1; i++)
        {
            sum += values[i];
        }
        current_value = (sum * MAX_BRIGHTNESS / ((NUM_SAMPLES - 2) * 4095));
        xSemaphoreTake(is_led_on_mutex, portMAX_DELAY);
        if (is_led_on == true && current_value != old_value)
        {
            old_value = current_value;
            color_value = current_value;
            xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            // printf("value %d \n", color_value);
        }
        xSemaphoreGive(is_led_on_mutex);
        // UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI("Read stack size", "High water mark: %d", stack_high_water_mark); // check số word còn lại trong stack
        vTaskDelay(10);
    }
}

void button_init(void)
{
    button_intr = xQueueCreate(1, sizeof(bool));
    gpio_config_t io_intr_config = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << GPIO_NUM_13),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE};
    gpio_config(&io_intr_config);
    gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
    gpio_isr_handler_add(GPIO_NUM_13, io_intr_handler, NULL);
    xTaskCreate(task_button_intr, "button_interrupt_task", 2048, NULL, 10, NULL);
}

void rmt_init(void)
{
    led_config_t *led_config = malloc(sizeof(led_config_t));
    led_config->led_chan = NULL;
    led_config->led_encoder = NULL;
    led_config->tx_config.loop_count = 0;
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = 26,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4};
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_config->led_chan)); // init RMT TX

    // init led strip encoder
    led_strip_encoder_config_t encoder_config = {
        .resolution = 10000000};
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_config->led_encoder));

    // kích hoạt rmt tx channel
    ESP_ERROR_CHECK(rmt_enable(led_config->led_chan));

    // reset led
    uint8_t led_data[3 * 8] = {0};
    ESP_ERROR_CHECK(rmt_transmit(led_config->led_chan, led_config->led_encoder, led_data, sizeof(led_data), &led_config->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_config->led_chan, portMAX_DELAY));

    // tạo task bật tắt led
    xTaskCreate(led_control, "led_control_task", 2048, led_config, 10, NULL);
}

void adc_init(void)
{
    adc_oneshot_unit_handle_t *adc1_handle = malloc(sizeof(adc_oneshot_unit_handle_t));
    adc_oneshot_unit_init_cfg_t adc_unit_cfg = {
        .unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_unit_cfg, adc1_handle));

    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc1_handle, ADC_CHANNEL_5, &adc_chan_cfg));
    xTaskCreate(adjust_brightness, "abjust brightness task", 2600, adc1_handle, 11, NULL);
}

void app_main(void)
{
    is_led_on_mutex = xSemaphoreCreateMutex();
    uart_init();
    button_init();
    rmt_init();
    adc_init();
    ESP_LOGI(TAG, "DONE");
}
