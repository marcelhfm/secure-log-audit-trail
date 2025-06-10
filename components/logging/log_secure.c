#include "log_secure.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/ccm.h"
#include "nvs.h"
#include "ringbuf_flash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SEC_NAMESPACE "log_secure_nvs"
#define NVS_BLOB_NAME "meta"
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

    err = nvs_set_blob(h, NVS_BLOB_NAME, &sec_meta, sz);
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
  uint32_t timestamp = esp_log_timestamp();
  // we don't really care about endianness, so we just memcpy this
  uint8_t ts_bytes[sizeof(timestamp)];
  memcpy(ts_bytes, &timestamp, sizeof(timestamp));
  ESP_LOGD(sec_tag, "logging secure. ts=%" PRIu32, timestamp);

  char buf[256];
  int off = snprintf(buf, sizeof(buf), "%10" PRIu32 " [%s]: ", timestamp, tag);

  if (off < 0 || off >= sizeof(buf)) {
    return ESP_ERR_INVALID_ARG;
  }

  va_list ap;
  va_start(ap, fmt);
  int msg_len = vsnprintf(buf + off, sizeof(buf) - off, fmt, ap);
  va_end(ap);

  if (msg_len < 0) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t pt_len = off + msg_len + 1; // include '\n'
  buf[pt_len - 1] = '\n';

  // AEAD CCM encryption
  uint8_t cipher[256];
  uint8_t tag_out[AEAD_TAG_SIZE];
  mbedtls_ccm_context ccm;
  mbedtls_ccm_init(&ccm);
  mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, sec_meta.key,
                     AEAD_KEY_SIZE * 8);

  // encrypt & tag
  int rc = mbedtls_ccm_encrypt_and_tag(
      &ccm, pt_len, sec_meta.prev_tag, AEAD_NONCE_SIZE, ts_bytes,
      sizeof(ts_bytes), (const uint8_t *)buf, cipher, tag_out, AEAD_TAG_SIZE);
  mbedtls_ccm_free(&ccm);

  if (rc != 0) {
    ESP_LOGE(sec_tag, "CCM encrypt failed: %d", rc);
    return ESP_FAIL;
  }

  // preparing payload
  size_t record_len =
      AEAD_NONCE_SIZE + sizeof(ts_bytes) + pt_len + AEAD_TAG_SIZE;
  uint8_t record[record_len];
  uint8_t *p = record;

  // prepend nonce
  memcpy(p, sec_meta.prev_tag, AEAD_NONCE_SIZE);
  p += AEAD_NONCE_SIZE;

  // store associated data (timestamp as bytes array)
  memcpy(p, ts_bytes, sizeof(ts_bytes));
  p += sizeof(ts_bytes);

  // ciphertext
  memcpy(p, cipher, pt_len);
  p += pt_len;

  // auth tag
  memcpy(p, tag_out, AEAD_TAG_SIZE);

  esp_err_t err = ringbuf_flash_write(&rb, record, record_len);
  if (err != ESP_OK) {
    ESP_LOGE(sec_tag, "flash write failed (%s), resetting meta",
             esp_err_to_name(err));
    ESP_ERROR_CHECK(reset_log_secure(&rb));
    return err;
  }

  memcpy(sec_meta.prev_tag, tag_out, AEAD_NONCE_SIZE);
  nvs_handle_t h;
  if (nvs_open(SEC_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_blob(h, NVS_BLOB_NAME, &sec_meta, sizeof(sec_meta));
    nvs_commit(h);
    nvs_close(h);
  }

  return ESP_OK;
};

void log_secure_uart_dump() {
  ESP_LOGD(sec_tag, "dumping secure logs...");
  uint8_t record[272];
  size_t rec_len;

  const char *hdr = "log_dump:";
  size_t hdr_len = strlen(hdr);

  // hex buffer: two ASCII chars per byte, plus one newline
  // Worst case: 2*MAX_RECORD_LEN chars + '\n'
  char hexbuf[2 * 272 + 1];

  while (!ringbuf_flash_empty(&rb)) {
    if (ringbuf_flash_read_record(&rb, record, sizeof(record), &rec_len) !=
        ESP_OK) {
      ESP_LOGE(sec_tag, "Error dumping logs. Resetting metadata.");
      ESP_ERROR_CHECK(reset_log_secure(&rb));
      return;
    }

    // send ASCII header + newline
    int ret = usb_serial_jtag_write_bytes(hdr, hdr_len, pdMS_TO_TICKS(1000));
    if (ret < 0) {
      ESP_LOGE(sec_tag, "error writing header over UART. ret=%d", ret);
      return;
    }

    // hex-encode the binary record
    for (size_t i = 0; i < rec_len; ++i) {
      // each byte → two hex chars
      sprintf(&hexbuf[2 * i], "%02X", record[i]);
    }
    // append newline
    hexbuf[2 * rec_len] = '\n';

    // send the hex string
    ret = usb_serial_jtag_write_bytes(hexbuf, 2 * rec_len + 1,
                                      pdMS_TO_TICKS(10000));
    if (ret < 0) {
      ESP_LOGE(sec_tag, "error writing hex record over UART. ret=%d", ret);
      return;
    }
  }

  const char *footer = "log_dump_end:";
  size_t footer_len = strlen(footer);
  int ret =
      usb_serial_jtag_write_bytes(footer, footer_len, pdMS_TO_TICKS(1000));
  if (ret < 0) {
    ESP_LOGE(sec_tag, "error writing footer over UART. ret=%d", ret);
    return;
  }
}
