#ifndef LIBJITTER_LIBJITTER_H
#define LIBJITTER_LIBJITTER_H

#include "Packet.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const void (*LibJitterConcealmentCallback)(struct Concealment *, const size_t num_packets, void* user_data);

/**
   * @brief Construct a new Jitter Buffer object.
   *
   * @param element_size Size of held elements in bytes.
   * @param clock_rate Clock rate of elements contained in Hz. E.g 48kHz audio is 48000.
   * @param max_length The maximum length of the buffer in milliseconds.
   * @param mix_length The minimum age of packets in milliseconds before eligible for dequeue.
   */
void *JitterInit(size_t element_size, size_t packet_elements, unsigned long clock_rate, unsigned long max_length_ms, unsigned long min_length_ms);

/// @brief Enqueue packets of data.
/// @param libjitter
/// @param packets Array of packets of data.
/// @param elements Number of packets in packets.
/// @param concealment_callback Callback fires when concealment data is requested.
/// @param free_callback Callback fires when the concealment packets are finished with.
/// @return Number of elements enqueued.
size_t JitterEnqueue(void *libjitter, const struct Packet packets[], size_t elements, LibJitterConcealmentCallback concealment_callback, void* user_data);

/// @brief Dequeue num elements from data into buffer.
/// @param libjitter The jitter buffer instance to dequeue from.
/// @param destination Pointer to copy bytes to.
/// @param destination_length Capacity of destination in bytes.
/// @param length Desired number of elements each of length element_size bytes to dequeue.
/// @return Number of elements each of length element_size bytes actually dequeued.
size_t JitterDequeue(void *libjitter, void *destination, size_t destination_length, size_t elements);

/// @brief Destroy a libjitter instance.
/// @param libjitter The jitter buffer instance to destroy.
void JitterDestroy(void *libjitter);
#ifdef __cplusplus
}
#endif
#endif