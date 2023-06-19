#ifndef LIBJITTER_LIBJITTER_H
#define LIBJITTER_LIBJITTER_H

#include "Packet.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const void (*LibJitterConcealmentCallback)(struct Packet *, const size_t num_packets);

/**
   * @brief Construct a new Jitter Buffer object.
   *
   * @param element_size Size of held elements in bytes.
   * @param clock_rate Clock rate of elements contained in Hz. E.g 48kHz audio is 48000.
   * @param max_length The maximum lenghth of the buffer in milliseconds.
   * @param mix_length The minimum age of packets in milliseconds before eligible for dequeue.
   */
void *JitterInit(const size_t element_size, const unsigned long clock_rate, const unsigned long max_length_ms, const unsigned long min_length_ms);

/// @brief Enqueue packets of data.
/// @param libjitter
/// @param packets Array of packets of data.
/// @param elements Number of packets in packets.
/// @param concealment_callback Callback fires when concealment data is requested.
/// @param free_callback Callback fires when the concealment packets are finished with.
/// @return Number of elements enqueued.
size_t JitterEnqueue(void *libjitter, const struct Packet packets[], const size_t elements, const LibJitterConcealmentCallback concealment_callback, const LibJitterConcealmentCallback free_callback);

/// @brief Dequeue num elements from data into buffer.
/// @param libjitter The jitter buffer instance to dequeue from.
/// @param destination Pointer to copy bytes to.
/// @param destination_length Capacity of destination in bytes.
/// @param length Desired number of elements each of length element_size bytes to dequeue.
/// @return Number of elements each of length element_size bytes actually dequeued.
size_t JitterDequeue(void *libjitter, void *destination, const size_t destination_length, const size_t elements);

/// @brief Destroy a libjitter instance.
/// @param libjitter The jitter buffer instance to destroy.
void JitterDestroy(void *libjitter);
#ifdef __cplusplus
}
#endif
#endif