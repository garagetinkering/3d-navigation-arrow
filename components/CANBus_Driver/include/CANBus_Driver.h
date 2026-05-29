#ifdef __cplusplus
extern "C" {
#endif

#include "driver/twai.h"
#include "esp_log.h"

#define CAN_TX_GPIO     (gpio_num_t)5
#define CAN_RX_GPIO     (gpio_num_t)4
#define CANBUS_SPEED    500000   // 500kbps

#define CAN_QUEUE_LENGTH 32
#define CAN_QUEUE_ITEM_SIZE sizeof(twai_message_t)
#ifndef CANBUS_TAG
#define CANBUS_TAG "TWAI"
#endif

extern bool receiving_data;
extern void (*can_message_handler)(twai_message_t *message);

void canbus_init(void);
void start_can_tasks(void);

#ifdef __cplusplus
}
#endif