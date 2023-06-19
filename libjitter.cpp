#include "libjitter.h"
#include "JitterBuffer.hh"

extern "C" {
void *JitterInit(const size_t element_size,
                 const unsigned long clock_rate,
                 const unsigned long max_length_ms,
                 const unsigned long min_length_ms) {
  return new JitterBuffer(element_size,
                          std::uint32_t(clock_rate),
                          std::chrono::milliseconds(max_length_ms),
                          std::chrono::milliseconds(min_length_ms));
}

size_t JitterEnqueue(void *libjitter,
                     const Packet packets[],
                     const size_t elements,
                     const LibJitterConcealmentCallback concealment_callback,
                     const LibJitterConcealmentCallback free_callback) {
  JitterBuffer *buffer = static_cast<JitterBuffer *>(libjitter);

  JitterBuffer::ConcealmentCallback callback = [concealment_callback](std::vector<Packet> &packets) {
    concealment_callback(&packets[0], packets.capacity());
  };

  JitterBuffer::ConcealmentCallback intermediate_free = [free_callback](std::vector<Packet> &packets) {
    free_callback(&packets[0], packets.capacity());
  };

  const std::vector<Packet> vector(packets, packets + elements);
  return buffer->Enqueue(vector,
                         callback,
                         intermediate_free);
}

size_t JitterDequeue(void *libjitter,
                     void *destination,
                     const size_t destination_length,
                     const size_t elements) {
  JitterBuffer *buffer = static_cast<JitterBuffer *>(libjitter);
  return buffer->Dequeue((std::uint8_t *) destination, destination_length, elements);
}

void JitterDestroy(void *libjitter) {
  delete static_cast<JitterBuffer *>(libjitter);
  libjitter = nullptr;
}
}
