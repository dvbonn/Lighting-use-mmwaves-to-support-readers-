#include "LD2410C.h"

static const uint8_t HEADER[] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t TAIL[] = {0x04, 0x03, 0x02, 0x01};
static const uint8_t Start_configuration[] = {0x04, 0x00, 0xFF, 0x00, 0x01, 0x00};
static const uint8_t On_engineering_mode[] = {0x02, 0x00, 0x62, 0x00};
static const uint8_t Off_engineering_mode[] = {0x02, 0x00, 0x63, 0x00};
static const uint8_t Set_energy_head_command[] = {0x14, 0x00, 0x64, 0x00};
static const uint8_t End_configuration[] = {0x02, 0x00, 0xFE, 0x00};
static const uint8_t Frame_header[] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t End_of_frame[] = {0xF8, 0xF7, 0xF6, 0xF5};
static uint8_t data[MAX_FRAME_LENGTH] = {0};
static uint8_t detection_state = NO_HUMAN;
QueueHandle_t ACK_confirm;
QueueHandle_t byte_ok;
bool wait_for_read = false;
static uint8_t dynamic_enviroment_energy[2][9] = {
    {MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY},
    {MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY}};
static uint8_t static_enviroment_energy[2][9] = {
    {MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY, MIN_ENERGY},
    {MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY, MAX_ENERGY}};
const char *LD2410C = "LD2410C";

void check_human_state(void)
{
    uint8_t data_human_state = data[8];
    int static_distance = (int)(data[9] | (data[10] >> 8));
    int dynamic_distance = (int)(data[12] | (data[13] >> 8));
    int detected_distance = (int)(data[15] | (data[16] >> 8));
    ESP_LOGI(LD2410C, "state %d dynamic %d static %d detected %d", (int)data[8], dynamic_distance, static_distance, detected_distance);
    if (data_human_state != detection_state)
    {
        switch (data_human_state)
        {
        case NO_HUMAN:
            detection_state = data[8];
            xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            break;
        case DYNAMIC_STATE:
            if (dynamic_distance <= 70 && detection_state == 0)
            {
                detection_state = data[8];
                xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            }
            else if (dynamic_distance > 70 && detection_state != 0)
            {
                detection_state = NO_HUMAN;
                xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            }
            break; 
        case STATIC_STATE:
            if (static_distance <= 70 && detection_state == 0)
            {
                detection_state = data[8];
                xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            }
            else if (static_distance > 70 && detection_state != 0)
            {
                detection_state = NO_HUMAN;
                xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            }
            break;
        case DYNAMIC_AND_STATIC_STATE:
            if (detected_distance <= 70 && detection_state == 0)
            {
                detection_state = data[8];
                xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            }
            else if (detected_distance > 70 && detection_state != 0)
            {
                detection_state = NO_HUMAN;
                xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
            }
            break;
        }
    }
}

void send_data(const uint8_t *command, size_t command_length)
{
    bool ack_ok = false;
    size_t total_length = sizeof(HEADER) + command_length + sizeof(TAIL);
    uint8_t *package_send = malloc(total_length);
    // gộp header, command, tail thành 1 package
    memcpy(package_send, HEADER, sizeof(HEADER));
    memcpy(package_send + sizeof(HEADER), command, command_length);
    memcpy(package_send + sizeof(HEADER) + command_length, TAIL, sizeof(TAIL));
    // send package
    esp_err_t ret;
    do{
        ret = uart_write_bytes(UART_NUM_2, (const char *)package_send, total_length);
        uart_wait_tx_done(UART_NUM_2, 500);
        // ESP_LOGI(LD2410C, "Send command: 0x%02X", package_send[6]);
        xQueueReceive(ACK_confirm, &ack_ok, portMAX_DELAY);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    } while(ack_ok != true) ;

    if (ret < 0)
    {
        ESP_LOGE(LD2410C, "Failed to write data to UART: %s", esp_err_to_name(ret));
    }
    free(package_send);
}

void read_data(void *arg)
{
    int index = 0;
    bool header_found = false;
    bool is_ack = false;
    bool ack_ok = false;
    while (1)
    {
        uint8_t byte;
        int length = uart_read_bytes(UART_NUM_2, &byte, 1, 100 / portTICK_PERIOD_MS); // Đọc từng byte
        if (length > 0)
        {
            // ESP_LOGI(LD2410C, "Received byte: 0x%02X", byte);
            if (!header_found)
            {
                // Kiểm tra header
                data[index++] = byte;
                if (index >= HEADER_LENGTH)
                {
                    if(memcmp(data, Frame_header, HEADER_LENGTH) == 0)
                    {
                        // Đã tìm thấy frame header
                        header_found = true;
                        index = HEADER_LENGTH;
                    }
                    else if(memcmp(data, HEADER, HEADER_LENGTH) == 0)
                    {
                        // Đã tìm thấy ack header
                        header_found = true;
                        is_ack = true;
                        index = HEADER_LENGTH;
                    }
                }
                
            }
            else
            {
                // Đã tìm thấy header, tiếp tục đọc cho đến khi tìm thấy end
                data[index++] = byte;
                if(index >= END_LENGTH)
                {
                    if (is_ack == false && memcmp(&data[index - END_LENGTH], End_of_frame, END_LENGTH) == 0)
                    {
                        // Đã tìm thấy end frame
                        // ESP_LOGI(LD2410C, "Frame received with length: %d", index);
                        if(index == 23)
                        {
                            check_human_state();
                        }
                        if(wait_for_read == true)
                        {
                            wait_for_read = false;
                            xQueueSend(byte_ok, &wait_for_read, portMAX_DELAY);
                        }
                        index = 0;
                        header_found = false; // Chuẩn bị đọc frame mới
                    }
                    else if (is_ack == true && memcmp(&data[index - END_LENGTH], TAIL, END_LENGTH) == 0)
                    {
                        // Đã tìm thấy end ack
                        // ESP_LOGI(LD2410C, "ACK received: 0x%02X", data[6]);
                        if(data[8] == 0x00 && data[9] == 0x00)
                        {
                            ack_ok = true;
                            xQueueSend(ACK_confirm, &ack_ok, 10 / portTICK_PERIOD_MS);    
                        }
                        index = 0;
                        is_ack = false;
                        ack_ok = false;
                        header_found = false; // Chuẩn bị đọc frame mới
                    }
                }
            }
        }
        if (index >= MAX_FRAME_LENGTH) {
            ESP_LOGE(LD2410C, "Full buffer");
            index = 0;
            uart_flush(UART_NUM_2);
            header_found = false;
        }
        // UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI("Read stack size", "High water mark: %d", stack_high_water_mark); // check số word còn lại trong stack
    }
}

static inline void update_min_max(uint8_t current_value, uint8_t *min, uint8_t *max)
{
    // ESP_LOGI(LD2410C, "current %d max %d min %d", current_value, *max, *min);
    if (current_value > *max)
    {
        *max = current_value;
    }
    else if (current_value < *min)
    {
        *min = current_value;
    }
}

static void calc_and_set_energy(void)
{
    for (int NUM_DOOR = MIN_DOOR_NUM; NUM_DOOR <= MAX_DOOR_NUM; NUM_DOOR++)
    {
        // tính toán energy
        uint8_t dynamic_set_energy = (dynamic_enviroment_energy[0][NUM_DOOR] + dynamic_enviroment_energy[1][NUM_DOOR]) / (uint8_t)2;
        uint8_t static_set_energy = (static_enviroment_energy[0][NUM_DOOR] + static_enviroment_energy[1][NUM_DOOR]) / (uint8_t)2;
        dynamic_set_energy = dynamic_set_energy > MAX_ENERGY ? MAX_ENERGY : dynamic_set_energy;
        static_set_energy = static_set_energy > MAX_ENERGY ? MAX_ENERGY : static_set_energy;
        // ESP_LOGI(LD2410C, "dynamic: %d static: %d", dynamic_set_energy, static_set_energy);
        // khai báo 3 part
        const uint8_t PART_NUM_DOOR[] = {0x00, 0x00, NUM_DOOR, 0x00, 0x00, 0x00};
        const uint8_t PART_DYNAMIC_ENERGY[] = {0x01, 0x00, dynamic_set_energy, 0x00, 0x00, 0x00};
        const uint8_t PART_STATIC_ENERGY[] = {0x02, 0x00, static_set_energy, 0x00, 0x00, 0x00};
        const size_t PART_NUM_DOOR_SIZE = sizeof(PART_NUM_DOOR);
        const size_t PART_DYNAMIC_ENERGY_SIZE = sizeof(PART_DYNAMIC_ENERGY);
        const size_t PART_STATIC_ENERGY_SIZE = sizeof(PART_STATIC_ENERGY);

        // gộp 4 part lại
        size_t command_length = sizeof(Set_energy_head_command) + PART_NUM_DOOR_SIZE + PART_DYNAMIC_ENERGY_SIZE + PART_STATIC_ENERGY_SIZE;
        ;
        uint8_t *command_send = malloc(command_length);
        size_t offset = 0;
        memcpy(command_send + offset, Set_energy_head_command, sizeof(Set_energy_head_command));
        offset += sizeof(Set_energy_head_command);

        memcpy(command_send + offset, PART_NUM_DOOR, PART_NUM_DOOR_SIZE);
        offset += PART_NUM_DOOR_SIZE;

        memcpy(command_send + offset, PART_DYNAMIC_ENERGY, PART_DYNAMIC_ENERGY_SIZE);
        offset += PART_DYNAMIC_ENERGY_SIZE;

        memcpy(command_send + offset, PART_STATIC_ENERGY, PART_STATIC_ENERGY_SIZE);

        // gửi command
        send_data(command_send, command_length);

        free(command_send);
    }
}

void noise_reduce(void *arg)
{
    handle_config_t *handle_config = (handle_config_t *)arg;
    send_data(Start_configuration, sizeof(Start_configuration));
    send_data(On_engineering_mode, sizeof(On_engineering_mode));
    send_data(End_configuration, sizeof(End_configuration));
    wait_for_read = true;
    xQueueReceive(byte_ok, &wait_for_read, portMAX_DELAY);
    esp_timer_start_once(handle_config->timer_handle, 5000000);
    ESP_LOGI(LD2410C, "Reducing noise");
    while (1)
    {
        for (int byte_position = FIRSTS_DYNAMIC_BYTE_POSITION; byte_position <= LAST_DYNAMIC_BYTE_POSITION; byte_position++)
        {
            update_min_max(data[byte_position], &dynamic_enviroment_energy[1][byte_position - FIRSTS_DYNAMIC_BYTE_POSITION], &dynamic_enviroment_energy[0][byte_position - FIRSTS_DYNAMIC_BYTE_POSITION]);
        }
        for (int byte_position = FIRSTS_STATIC_BYTE_POSITION; byte_position <= LAST_STATIC_BYTE_POSITION; byte_position++)
        {
            update_min_max(data[byte_position], &static_enviroment_energy[1][byte_position - FIRSTS_STATIC_BYTE_POSITION], &static_enviroment_energy[0][byte_position - FIRSTS_STATIC_BYTE_POSITION]);
        }
        vTaskDelay(100);
    }
}

void stop_engineering_mode(void *arg)
{
    detection_state = NO_HUMAN;
    handle_config_t *handle_config = (handle_config_t *)arg;
    send_data(Start_configuration, sizeof(Start_configuration));
    calc_and_set_energy();
    send_data(Off_engineering_mode, sizeof(Off_engineering_mode));
    send_data(End_configuration, sizeof(End_configuration));
    uart_flush(UART_NUM_2);
    esp_timer_delete(handle_config->timer_handle);
    vTaskDelete(handle_config->noise_task_handle);
    free(handle_config);
    gpio_intr_enable(GPIO_NUM_13);    
    xQueueSend(detection_queue, &detection_state, portMAX_DELAY);
    ESP_LOGI(LD2410C, "Done reduce");
}

void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .stop_bits = UART_STOP_BITS_1,
        .parity = UART_PARITY_DISABLE,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .data_bits = UART_DATA_8_BITS,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, RX_BUFFER_SIZE, TX_BUFFER_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, GPIO_NUM_17, GPIO_NUM_16, -1, -1));
    ACK_confirm = xQueueCreate(1, sizeof(bool)); 
    detection_queue = xQueueCreate(1, sizeof(uint8_t)); 
    byte_ok = xQueueCreate(1, sizeof(bool));
    xTaskCreate(read_data, "read_data_task", 2048, NULL, 10, NULL);
    if (uart_is_driver_installed(UART_NUM_2) == ESP_OK)
    {
        ESP_LOGI(LD2410C, "Uart init successfull");
    }
}