#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_plain.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

const char *main_tag = "main";

void app_main(void) {
  nvs_flash_init();

  esp_err_t err = log_plain_init();
  if (err != ESP_OK) {
    ESP_LOGE(main_tag, "could not initialize log_plain err=%s",
             esp_err_to_name(err));
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
  }

  for (int i = 10; i >= 0; i--) {
    log_plain(main_tag, "Restarting in %d seconds...", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  log_plain(main_tag, "Restarting now.");

  log_plain_uart_dump();

  fflush(stdout);
  esp_restart();
}
