#include "ring_buffer.h"
#include "esp_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct ring_buffer_t {
  uint8_t *buf;    // start of buffer memory
  size_t capacity; // total size
  size_t head;     // write index
  size_t tail;     // read index
  uint8_t full;    // buffer full flag
};

static const char *TAG = "ring_buffer";

ring_buffer_t *ring_buffer_init(void *base_addr, size_t capacity) {
  if (!base_addr || capacity == 0) {
    ESP_LOGE(TAG, "invalid params for ring buffer init.");
    return NULL;
  }

  ring_buffer_t *rb = calloc(1, sizeof(ring_buffer_t));
  if (!rb) {
    ESP_LOGE(TAG, "failed to allocate ring buffer control");
  }

  rb->buf = (uint8_t *)base_addr;
  rb->capacity = capacity;
  rb->head = 0;
  rb->tail = 0;
  rb->full = 0;

  return rb;
}

void ring_buffer_deinit(ring_buffer_t *rb) {
  if (rb) {
    free(rb);
  }
}

int ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len) {
  if (!rb || !data || len == 0) {
    return -1;
  }

  const uint8_t *src = data;
  for (size_t i = 0; i < len; i++) {
    rb->buf[rb->head] = src[i];
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->full) {
      // Overwrite: advance tail
      rb->tail = (rb->tail + 1) % rb->capacity;
    }
    // if head catches tail, buffer is full
    rb->full = (rb->head == rb->tail);
  }

  return 0;
}

size_t ring_buffer_available(const ring_buffer_t *rb) {
  if (!rb)
    return 0;

  // fully occupied: head == tail and full==true
  if (rb->full) {
    return rb->capacity;
  }

  // no wrap: data from tail up to head
  if (rb->head >= rb->tail) {
    return rb->head - rb->tail;
  }

  // wrapped: data from tail→capacity and 0→head
  return rb->capacity + rb->head - rb->tail;
}

size_t ring_buffer_read(ring_buffer_t *rb, void *dst, size_t len) {
  if (!rb || !dst || len == 0)
    return 0;

  size_t avail = ring_buffer_available(rb);
  size_t to_read = (len < avail) ? len : avail;
  uint8_t *dst8 = dst;
  for (size_t i = 0; i < to_read; i++) {
    dst8[i] = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->full = 0;
  }

  return to_read;
}
