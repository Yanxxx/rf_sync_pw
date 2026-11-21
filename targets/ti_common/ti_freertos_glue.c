// Small pieces of SysConfig's ti_freertos_config.c that are not part of the
// FreeRTOS kernel or TI DPL sources. Keep in sync if SysConfig changes them.

#include <FreeRTOS.h>
#include <queue.h>

// Wrappers used by TI's DPL; the queue registry is disabled
// (configQUEUE_REGISTRY_SIZE == 0).
void vQueueAddToRegistryWrapper(QueueHandle_t xQueue, const char* pcQueueName) {
  (void)xQueue;
  (void)pcQueueName;
}

void vQueueUnregisterQueueWrapper(QueueHandle_t xQueue) { (void)xQueue; }
