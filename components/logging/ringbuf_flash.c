#include "ringbuf_flash.h"
#include "esp_crc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/idf_additions.h"
#include "nvs.h"
#include "portmacro.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define NVS_NAMESPACE "rb_log"
#define META_VERSION 1

static const char *rb_tag = "ringbuf_flash";

static esp_err_t save_meta(ringbuf_flash_t *rb, const rb_meta_t *m) {
  nvs_handle_t h;
  ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));

  rb_meta_t tmp = *m;

  // compute crc over first four fields
  tmp.crc32 = esp_crc32_le(0, (uint8_t *)&tmp, offsetof(rb_meta_t, crc32));

  esp_err_t err = nvs_set_blob(h, rb->label, &tmp, sizeof(tmp));
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err;
}

static esp_err_t load_meta(ringbuf_flash_t *rb, rb_meta_t *m) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    return err;
  }

  size_t len = sizeof(*m);
  err = nvs_get_blob(h, rb->label, m, &len);
  if (err == ESP_OK && len == sizeof(*m)) {
    // verify version
    if (m->version != META_VERSION) {
      err = ESP_ERR_INVALID_VERSION;
    } else {
      // verify crc
      uint32_t crc = esp_crc32_le(0, (uint8_t *)m, offsetof(rb_meta_t, crc32));
      if (crc != m->crc32) {
        err = ESP_ERR_INVALID_CRC;
      }
    }
  }

  nvs_close(h);

  if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_VERSION ||
      err == ESP_ERR_INVALID_CRC) {
    // treat as fresh
    m->head = 0;
    m->tail = 0;
    m->cycle = 0;
    m->version = META_VERSION;
    m->crc32 = 0;

    // erase partition once
    ESP_ERROR_CHECK(esp_partition_erase_range(rb->part, 0, rb->part->size));
    return save_meta(rb, m);
  }

  return err;
}

esp_err_t ringbuf_flash_init(ringbuf_flash_t *rb, const char *partition_label) {
  // find partition
  rb->part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partition_label);
  if (!rb->part) {
    return ESP_ERR_NOT_FOUND;
  }
  ESP_LOGI(rb_tag, "partition find successful. label=%s", partition_label);

  rb->label = partition_label;

  // init lock
  rb->lock = xSemaphoreCreateMutex();
  if (rb->lock == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = load_meta(rb, &rb->meta);
  if (err != ESP_OK) {
    return err;
  }

  // on first boot erase everything
  err = esp_partition_erase_range(rb->part, 0, rb->part->size);
  if (err != ESP_OK) {
    ESP_LOGE(rb_tag, "error while erasing whole partition. err=%s",
             esp_err_to_name(err));
    return err;
  }

  return ESP_OK;
}

esp_err_t ringbuf_flash_write(ringbuf_flash_t *rb, const void *data,
                              size_t len) {
  xSemaphoreTake(rb->lock, portMAX_DELAY);

  if (len > rb->part->size) {
    xSemaphoreGive(rb->lock);
    ESP_LOGE(
        rb_tag,
        "write: data too long for partition. len=%zu partition_len=%" PRIu32,
        len, rb->part->size);
    return ESP_ERR_INVALID_SIZE;
  }

  // build header
  rbf_rec_hdr_t hdr = {
      .magic = RBF_MAGIC, .len = len, .crc = esp_crc32_le(0, data, len)};

  // write header
  esp_err_t err =
      esp_partition_write(rb->part, rb->meta.head, &hdr, sizeof(hdr));
  if (err != ESP_OK) {
    xSemaphoreGive(rb->lock);
    ESP_LOGE(rb_tag, "write: error while writing header. err=%s",
             esp_err_to_name(err));
    return err;
  }

  // write data
  err = esp_partition_write(rb->part, rb->meta.head + sizeof(hdr), data, len);
  if (err != ESP_OK) {
    xSemaphoreGive(rb->lock);
    ESP_LOGE(rb_tag, "error while writing data. err=%s", esp_err_to_name(err));
    return err;
  }

  // advance head
  rb->meta.head += sizeof(hdr) + len;

  // wrap around
  if (rb->meta.head >= rb->part->size) {
    rb->meta.head = 0;
    rb->meta.cycle++;

    // TODO: Erase part->erase_size of section
  }

  save_meta(rb, &rb->meta);

  xSemaphoreGive(rb->lock);
  return ESP_OK;
}

esp_err_t ringbuf_flash_dump(ringbuf_flash_t *rb, void *out_buf,
                             size_t buf_size, size_t *out_len) {
  xSemaphoreTake(rb->lock, portMAX_DELAY);

  uint32_t pos = 0;
  uint32_t wrote = 0;

  while (pos < rb->meta.head && wrote < buf_size) {
    rbf_rec_hdr_t hdr;
    ESP_ERROR_CHECK(esp_partition_read(rb->part, pos, &hdr, sizeof(hdr)));
    if (hdr.magic != RBF_MAGIC || hdr.len + wrote > buf_size) {
      break;
    }

    void *dst = (uint8_t *)out_buf + wrote;
    ESP_ERROR_CHECK(
        esp_partition_read(rb->part, pos + sizeof(hdr), dst, hdr.len));

    // validate crc
    uint32_t crc = esp_crc32_le(0, dst, hdr.len);
    if (crc != hdr.crc) {
      ESP_LOGW(rb_tag, "crc missmatch at pos=%" PRIu32, pos);
      break;
    }

    pos += sizeof(hdr) + hdr.len;
    wrote += hdr.len;
  }

  *out_len = wrote;
  xSemaphoreGive(rb->lock);
  return ESP_OK;
}

esp_err_t ringbuf_flash_read_record(ringbuf_flash_t *rb, void *out_buf,
                                    size_t buf_size, size_t *out_len) {
  xSemaphoreTake(rb->lock, portMAX_DELAY);
  // read header
  rbf_rec_hdr_t hdr;
  if (rb->meta.tail + sizeof(hdr) > rb->part->size) {
    // wrap around
    rb->meta.tail = 0;
  }

  esp_err_t err =
      esp_partition_read(rb->part, rb->meta.tail, &hdr, sizeof(hdr));
  if (err != ESP_OK) {
    xSemaphoreGive(rb->lock);
    return err;
  }

  if (hdr.magic != RBF_MAGIC) {
    ESP_LOGE(rb_tag, "read: could not verify header hdr=%" PRIu32, hdr.magic);
    xSemaphoreGive(rb->lock);
    return ESP_ERR_NOT_FOUND;
  }

  // read payload
  uint32_t payload_offset = rb->meta.tail + sizeof(hdr);
  if (payload_offset + hdr.len > rb->part->size) {
    // wrap around case
    uint32_t first = rb->part->size - payload_offset;
    esp_partition_read(rb->part, payload_offset, out_buf, first);
    esp_partition_read(rb->part, 0, ((uint8_t *)out_buf) + first,
                       hdr.len - first);
  } else {
    esp_partition_read(rb->part, payload_offset, out_buf, hdr.len);
  }

  uint32_t crc = esp_crc32_le(0, out_buf, hdr.len);
  if (crc != hdr.crc) {
    ESP_LOGW(rb_tag, "read: crc missmatch at tail=%" PRIu32, rb->meta.tail);
    xSemaphoreGive(rb->lock);
    return ESP_ERR_INVALID_CRC;
  }

  // advance tail
  rb->meta.tail = (rb->meta.tail + sizeof(hdr) + hdr.len) % rb->part->size;
  save_meta(rb, &rb->meta);

  *out_len = hdr.len;
  xSemaphoreGive(rb->lock);
  return ESP_OK;
}
