#include "log_secure.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "ringbuf_flash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SEC_NAMESPACE "log_secure_nvs"
#define AEAD_KEY_SIZE 32
#define AEAD_NONCE_SIZE 12
#define AEAD_TAG_SIZE 16

static const char *sec_tag = "log_secure";

typedef struct {
  uint8_t key[AEAD_KEY_SIZE];
  uint8_t prev_tag[AEAD_NONCE_SIZE];
} sec_meta_t;

static ringbuf_flash_t rb;
static sec_meta_t sec_meta;

esp_err_t log_secure_init() {
  esp_err_t err = ringbuf_flash_init(&rb, "log_secure");
  if (err != ESP_OK) {
    return err;
  }

  nvs_handle_t h;
  err = nvs_open(SEC_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    return err;
  }
  size_t sz = sizeof(sec_meta);
  err = nvs_get_blob(h, "meta", &sec_meta, &sz);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // first boot: generate key + nonce
    ESP_LOGD(sec_tag, "detected first boot. generating aead key");

    esp_fill_random(sec_meta.key, AEAD_KEY_SIZE);
    esp_fill_random(sec_meta.prev_tag, AEAD_NONCE_SIZE);

    err = nvs_set_blob(h, "meta", &sec_meta, sz);
    if (err == ESP_OK) {
      err = nvs_commit(h);
    }
  }
  nvs_close(h);

  return err;
}

esp_err_t reset_log_secure(ringbuf_flash_t *rb) {
  ESP_ERROR_CHECK(reset_meta(rb));

  nvs_handle_t h;

  ESP_ERROR_CHECK(nvs_open(SEC_NAMESPACE, NVS_READWRITE, &h));
  ESP_ERROR_CHECK(nvs_erase_key(h, "meta"));
  ESP_ERROR_CHECK(nvs_commit(h));
  nvs_close(h);
  // 3) re-init both ringbuf and sec_meta
  return log_secure_init();
}

esp_err_t log_secure(const char *tag, const char *fmt, ...) {
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
  esp_err_t err = ringbuf_flash_write(&rb, buf, total);
  if (err != ESP_OK) {
    ESP_LOGE(sec_tag, "Error writing logs. Resetting metadata.");
    ESP_ERROR_CHECK(reset_meta(&rb));
  }

  return err;
};

void log_secure_uart_dump() {
  ESP_LOGD(sec_tag, "dumping logs...");
  uint8_t buf[256];
  size_t rec_len;
  while (!ringbuf_flash_empty(&rb)) {
    if (ringbuf_flash_read_record(&rb, buf, sizeof(buf), &rec_len) == ESP_OK) {
      // TODO: Send over uart
      ESP_LOGI(sec_tag, "Read Log (length=%zu): '%.*s'", rec_len, (int)rec_len,
               buf);
    } else {
      ESP_LOGE(sec_tag, "Error dumping logs. Resetting metadata.");
      ESP_ERROR_CHECK(reset_meta(&rb));
      return;
    }
  }
}
