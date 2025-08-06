#pragma once

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task.h"
#include "init.h"
#include <string.h>

#define BAUD_RATE 256000
#define RX_BUFFER_SIZE 1024
#define TX_BUFFER_SIZE 1024
#define MAX_FRAME_LENGTH 45
#define HEADER_LENGTH 4
#define END_LENGTH 4
#define FIRSTS_BYTE_POSITION 0
#define FIRSTS_DYNAMIC_BYTE_POSITION 19
#define LAST_DYNAMIC_BYTE_POSITION 27
#define FIRSTS_STATIC_BYTE_POSITION 28
#define LAST_STATIC_BYTE_POSITION 36
#define MAX_ENERGY (uint8_t)90
#define MIN_ENERGY (uint8_t)0
#define MAX_DOOR_NUM 8
#define MIN_DOOR_NUM 0

enum __state {
    NO_HUMAN,
    DYNAMIC_STATE,
    STATIC_STATE,
    DYNAMIC_AND_STATIC_STATE,
    REDUCE_STATE
};

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_timer_handle_t timer_handle;
    TaskHandle_t noise_task_handle;
} handle_config_t;

void send_data(const uint8_t *command, size_t command_length);
void read_data(void *arg);
void noise_reduce(void *arg);
void stop_engineering_mode(void *arg);
void uart_init(void);



#ifdef __cplusplus
}
#endif