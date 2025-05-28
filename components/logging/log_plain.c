#include "log_plain.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ringbuf_flash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const char *plain_tag = "log_plain";
static ringbuf_flash_t rb;

esp_err_t log_plain_init() {
  esp_err_t err = ringbuf_flash_init(&rb, "log_plain");
  if (err != ESP_OK) {
    return err;
  }

  return ESP_OK;
}

esp_err_t log_plain(const char *tag, const char *fmt, ...) {
  char buf[256];
  int off = snprintf(buf, sizeof(buf),
                     "%10" PRIu32 " [%s]: ", esp_log_timestamp(), tag);

  if (off < 0 || off >= sizeof(buf)) {
    return ESP_ERR_INVALID_ARG;
  }

  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf + off, sizeof(buf) - off, fmt, ap);
  va_end(ap);

  if (len < 0) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t total = off + len;
  if (total + 1 > sizeof(buf)) {
    total = sizeof(buf) - 1;
  }

  buf[total++] = '\n';
  return ringbuf_flash_write(&rb, buf, total);
};

void log_plain_uart_dump() {
  uint8_t buf[256];
  size_t rec_len;
  while (!ringbuf_flash_empty(&rb)) {
    if (ringbuf_flash_read_record(&rb, buf, sizeof(buf), &rec_len) == ESP_OK) {
      // TODO: Send over uart
      ESP_LOGI(plain_tag, "Read Log (length=%zu): '%.*s'", rec_len,
               (int)rec_len, buf);
    }
  }
}
