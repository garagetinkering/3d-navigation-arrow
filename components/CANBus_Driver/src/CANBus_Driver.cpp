#include "CANBus_Driver.h"

QueueHandle_t canMsgQueue;

void receive_can_task(void *arg) {
  while (1) {
    twai_message_t message;
    esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(5)); // lower timeout for faster response
    if (err == ESP_OK) {
      ESP_LOGI(CANBUS_TAG, "Received CAN message");
      receiving_data = true;
      if (xQueueSend(canMsgQueue, &message, 0) != pdPASS) {
        ESP_LOGE(CANBUS_TAG, "Failed to send CAN message to queue");
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      // No delay after successful receive
    } else if (err == ESP_ERR_TIMEOUT) {
        receiving_data = false;
      // Minimal delay when idle
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
       receiving_data = false;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void process_can_queue_task(void *arg) {
  twai_message_t message;
  while (1) {
    if (xQueueReceive(canMsgQueue, &message, pdMS_TO_TICKS(1)) == pdPASS) {
            if (can_message_handler) {
                can_message_handler(&message);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void canbus_init(void) {
  // Configure TWAI (CAN)
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();  // Accept all IDs
 
    ESP_LOGI(CANBUS_TAG, "Starting TWAI driver installation");
    // Install and start TWAI driver
    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();


    // setup TWAI
}

void start_can_tasks(void) {
  canMsgQueue = xQueueCreate(CAN_QUEUE_LENGTH, CAN_QUEUE_ITEM_SIZE);
  if (canMsgQueue == NULL) {
    ESP_LOGE(CANBUS_TAG, "Failed to create CAN message queue");
    while (1) vTaskDelay(1000);
  }

  xTaskCreatePinnedToCore(receive_can_task, "Receive_CAN_Task", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(process_can_queue_task, "Process_CAN_Queue_Task", 4096, NULL, 2, NULL, 1);
  ESP_LOGI(CANBUS_TAG, "CAN tasks started");
}