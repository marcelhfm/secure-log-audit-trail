#pragma once

#include <stddef.h>

typedef struct ring_buffer_t ring_buffer_t;

/**
 * Initialize a ring buffer over a pre-allocated memory region.
 *
 * @param base_addr  Pointer to start of buffer memory.
 * @param capacity   Total capacity in bytes.
 * @return           Pointer to ring_buffer_t instance, or NULL on error.
 */
ring_buffer_t *ring_buffer_init(void *base_addr, size_t capacity);

/**
 * Free any state (does not free base_addr buffer).
 */
void ring_buffer_deinit(ring_buffer_t *rb);

/**
 * Write data into the ring buffer. If wrap occurs, oldest data is overwritten.
 *
 * @param rb     Ring buffer handle.
 * @param data   Pointer to data to write.
 * @param len    Number of bytes to write.
 * @return       true on success.
 */
int ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len);

/**
 * Read up to len bytes from the ring buffer into dst.
 *
 * @param rb     Ring buffer handle.
 * @param dst    Destination buffer.
 * @param len    Max bytes to read.
 * @return       Number of bytes actually read.
 */
size_t ring_buffer_read(ring_buffer_t *rb, void *dst, size_t len);

/**
 * Get number of bytes currently stored.
 */
size_t ring_buffer_available(const ring_buffer_t *rb);
