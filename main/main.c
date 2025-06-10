#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "log_secure.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UART_BUF_SIZE 102

const char *main_tag = "main";

void handle_command(const char *cmd) {
  ESP_LOGI(main_tag, "received command. cmd=%s", cmd);
  if (strcmp(cmd, "log_dump") == 0) {
    log_secure_uart_dump();
  } else if (strcmp(cmd, "ping") == 0) {
    const char *resp = "ping:pong\n";
    usb_serial_jtag_write_bytes(resp, strlen(resp), pdMS_TO_TICKS(10000));
  } else {
    ESP_LOGW(main_tag, "unknown command. cmd=%s", cmd);
  }
}

void uart_command_listener_task(void *arg) {
  ESP_LOGI(main_tag, "Hello form uart_command_listener_task");
  uint8_t *data = (uint8_t *)malloc(UART_BUF_SIZE);
  while (1) {
    int len =
        usb_serial_jtag_read_bytes(data, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
    if (len > 0) {
      data[len] = '\0';
      char *newline = strchr((char *)data, '\n');
      if (newline) {
        *newline = '\0';
      }
      handle_command((char *)data);
    }
  }

  free(data);
  vTaskDelete(NULL);
}

void periodic_log_task(void *arg) {
  ESP_LOGI(main_tag, "Hello form periodic_log_task");
  int counter = 0;
  while (1) {
    log_secure(main_tag, "system running smoothly. counter=%d", counter);
    counter++;
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  vTaskDelete(NULL);
}

void app_main(void) {
  nvs_flash_init();

  usb_serial_jtag_driver_config_t config =
      USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));

  ESP_ERROR_CHECK(log_secure_init());

  // start tasks
  xTaskCreate(uart_command_listener_task, "uart_command_listener", 4096, NULL,
              10, NULL);
  xTaskCreate(periodic_log_task, "periodic_log", 4096, NULL, 5, NULL);
}
