#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "max30102.h"

void app_main(void) {
	xTaskCreate(max30102_monitor_task, "max30102_monitor_task", 4096, NULL, 5, NULL);
}
