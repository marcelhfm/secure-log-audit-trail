#include "log_plain.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "ringbuf_flash.h"
#include <stddef.h>
#include <stdint.h>

/*
static const esp_partition_t *s_plain_part;
static ring_buffer_t *s_plain_rb;
static const void *s_plain_map;

esp_err_t log_plain_init(void) {
  // find partition by label
  s_plain_part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "log_plain");
  if (!s_plain_part) {
    return ESP_ERR_NOT_FOUND;
  }

  // memory map entire region for easy read/write
  esp_err_t err =
      esp_partition_mmap(s_plain_part, 0, s_plain_part->size,
                         ESP_PARTITION_MMAP_DATA, &s_plain_map, NULL);
  if (err != ESP_OK) {
    return err;
  }

  // TODO:
  // recover saved head/tail pointers from log_meta
  // read values from log_meta area

  // init ring buffer over the flash window
  s_plain_rb = ring_buffer_init((void *)s_plain_map, s_plain_part->size);
  if (!s_plain_rb) {
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t log_plain(const void *data, size_t len) {
  if (!s_plain_rb) {
    return ESP_ERR_INVALID_STATE;
  }

  ring_buffer_write(s_plain_rb, data, len);

  return ESP_OK;
}

esp_err_t log_plain_read(uint8_t *dst, size_t len, size_t *out_read) {
  if (!s_plain_rb) {
    return ESP_ERR_INVALID_STATE;
  }

  *out_read = ring_buffer_read(s_plain_rb, dst, len);
  return ESP_OK;
}
*/
