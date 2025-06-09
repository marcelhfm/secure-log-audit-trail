#pragma once

#include "esp_err.h"
#include "esp_partition.h"
#include "freertos/idf_additions.h"
#include <stddef.h>
#include <stdint.h>

#define RBF_MAGIC 0xABCD1234

typedef struct {
  uint32_t head;
  uint32_t tail;
  uint32_t cycle;
  uint32_t version;
  uint32_t crc32;
} rb_meta_t;

typedef struct {
  const esp_partition_t *part; // flash partition
  const char *label;           // partition label, used later for nvs
  rb_meta_t meta;
  SemaphoreHandle_t lock; // thread safety
} ringbuf_flash_t;

typedef struct {
  uint32_t magic; // sanity check
  uint32_t len;
  uint32_t crc;
} __attribute__((packed)) rbf_rec_hdr_t;

esp_err_t ringbuf_flash_init(ringbuf_flash_t *rb, const char *partition_label);

esp_err_t ringbuf_flash_write(ringbuf_flash_t *rb, const void *data,
                              size_t len);

esp_err_t ringbuf_flash_dump(ringbuf_flash_t *rb, void *out_buf,
                             size_t buf_size, size_t *out_len);

esp_err_t ringbuf_flash_read_record(ringbuf_flash_t *rb, void *out_buf,
                                    size_t buf_size, size_t *out_len);

esp_err_t reset_meta(ringbuf_flash_t *rb);

static inline bool ringbuf_flash_empty(const ringbuf_flash_t *rb) {
  return rb->meta.head == rb->meta.tail;
}
