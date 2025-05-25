#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ringbuf_flash.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char *main_tag = "main";

void app_main(void) {
  nvs_flash_init();

  ringbuf_flash_t rb;
  esp_err_t err = ringbuf_flash_init(&rb, "log_plain");
  if (err != ESP_OK) {
    ESP_LOGE(main_tag, "Flash init failed with %s", esp_err_to_name(err));
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    esp_restart();
  }

  const char *msgs[] = {"foo", "longer test message", "1234567890"};
  for (int i = 0; i < 3; i++) {
    err = ringbuf_flash_write(&rb, msgs[i], strlen(msgs[i]));
    if (err != ESP_OK) {
      ESP_LOGE(main_tag, "Flash write failed with %s", esp_err_to_name(err));
      break;
    }
  }

  uint8_t buf[256];
  size_t rec_len;
  while (!ringbuf_flash_empty(&rb)) {
    if (ringbuf_flash_read_record(&rb, buf, sizeof(buf), &rec_len) == ESP_OK) {
      ESP_LOGI(main_tag, "Read Record (length=%zu): '%.*s'", rec_len,
               (int)rec_len, buf);
    }
  }

  char dump[256];
  size_t len;
  err = ringbuf_flash_dump(&rb, dump, sizeof(dump), &len);
  if (err != ESP_OK) {
    ESP_LOGE(main_tag, "Flash read failed with %s", esp_err_to_name(err));
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
  ESP_LOGI(main_tag, "DUMP(%zu): '%.*s'", len, (int)len, dump);

  for (int i = 10; i >= 0; i--) {
    printf("Restarting in %d seconds...\n", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  printf("Restarting now.\n");
  fflush(stdout);
  esp_restart();
}
