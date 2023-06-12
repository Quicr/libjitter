#pragma once
#include "Packet.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class JitterBuffer {
  public:
  /**
     * @brief Provides concealment data.
     * 
     * @param elements The number of concealment elements to generate.
     * @return std::vector<Packet> The generated concealment packets.
     */
  typedef std::function<const std::vector<Packet>(const std::size_t& elements)> ConcealmentCallback;

  /**
     * @brief Construct a new Jitter Buffer object.
     * 
     * @param element_size Size of held elements in bytes.
     * @param clock_rate Clock rate of elements contained in Hz. E.g 48kHz audio is 48000.
     * @param max_length The maximum lenghth of the buffer in milliseconds.
     * @param min_length The minimum age of packets in milliseconds before eligible for dequeue.
     */
  JitterBuffer(const std::size_t element_size, const std::uint32_t clock_rate, const std::chrono::milliseconds max_length, const std::chrono::milliseconds min_length);
    
  ~JitterBuffer();

  /**
     * @brief Enqueue a number of packets onto the buffer. This must be called from a single writer thread.
     * 
     * @param packets The packets to enqueue.
     * @param concealment_callback Fired when concealment data needs to be generated.
     * @return std::size_t The number of packets actually enqueued, including concealment.
     */
  std::size_t Enqueue(const std::vector<Packet>& packets, const ConcealmentCallback& concealment_callback);

  /**
     * @brief Dequeue a number of packets into the given destination. This must be called from a single reader thread.
     * 
     * @param destination The buffer to copy the data into.
     * @param destination_length Length of destination buffer in bytes.
     * @param elements The number of elements to dequeue.
     * @return std::size_t The number of elements actually dequeued.
     */
  std::size_t Dequeue(void *destination, const std::size_t& destination_length, const std::size_t& elements);

  private:
  std::size_t element_size;
  std::chrono::milliseconds clock_rate;
  std::chrono::milliseconds min_length;

  std::uint8_t *buffer;
  std::uint8_t read_offset;
  std::uint8_t write_offset;
  std::size_t max_size_bytes;
  std::atomic<std::size_t> written;
  unsigned long last_written_sequence_number;
  unsigned long last_read_sequence_number;

  std::uint8_t* Back(std::size_t &available) const;
  std::uint8_t* Front(std::size_t &available) const;
  bool Copy(const Packet& packet);
  bool Update(const Packet &packet);
};
