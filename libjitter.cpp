#include "libjitter.h"
#include "JitterBuffer.hh"

#include <iostream>

extern "C" {
void *JitterInit(const size_t element_size,
                 const size_t packet_elements,
                 const unsigned long clock_rate,
                 const unsigned long max_length_ms,
                 const unsigned long min_length_ms,
                 cantina::Logger *logger) {
  return new JitterBuffer(element_size,
                          packet_elements,
                          std::uint32_t(clock_rate),
                          std::chrono::milliseconds(max_length_ms),
                          std::chrono::milliseconds(min_length_ms),
                          cantina::LoggerPointer(logger));
}

size_t JitterPrepare(void *libjitter,
                     const unsigned long sequence_number,
                     const LibJitterConcealmentCallback concealment_callback,
                     void *user_data) {
  auto *buffer = static_cast<JitterBuffer *>(libjitter);
  JitterBuffer::ConcealmentCallback callback = [concealment_callback, user_data](std::vector<Packet> &packets) {
    concealment_callback(&packets[0], packets.capacity(), user_data);
  };
  try {
    return buffer->Prepare(sequence_number, callback);
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << std::endl;
    return 0;
  }
}

size_t JitterEnqueue(void *libjitter,
                     const Packet packets[],
                     const size_t elements,
                     const LibJitterConcealmentCallback concealment_callback,
                     void *user_data) {
  auto *buffer = static_cast<JitterBuffer *>(libjitter);

  JitterBuffer::ConcealmentCallback callback = [concealment_callback, user_data](std::vector<Packet> &packets) {
    concealment_callback(&packets[0], packets.capacity(), user_data);
  };

  const std::vector<Packet> vector(packets, packets + elements);
  try {
    return buffer->Enqueue(vector, callback);
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << std::endl;
    return 0;
  }
}

size_t JitterDequeue(void *libjitter,
                     void *destination,
                     const size_t destination_length,
                     const size_t elements) {
  try {
    auto *buffer = static_cast<JitterBuffer *>(libjitter);
    return buffer->Dequeue((std::uint8_t *) destination, destination_length, elements);
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << std::endl;
    return 0;
  }
}

void JitterDestroy(void *libjitter) {
  delete static_cast<JitterBuffer *>(libjitter);
}
}
