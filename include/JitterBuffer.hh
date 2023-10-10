#pragma once

#include "Packet.h"

#include <cantina/logger.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

struct Header {
  std::uint32_t sequence_number;
  std::size_t elements;
  std::uint64_t timestamp;
  bool concealment;
  std::atomic_flag in_use = ATOMIC_FLAG_INIT;
  std::size_t previous_elements;
};

class JitterBuffer {
  public:
  const static std::size_t METADATA_SIZE = sizeof(Header);

  typedef std::function<void(std::vector<Packet> &packets)> ConcealmentCallback;

  /**
   * @brief Construct a new Jitter Buffer object.
   *
   * @param element_size Size of held elements in bytes.
   * @param packet_elements Number of elements in packets.
   * @param clock_rate Clock rate of elements contained in Hz. E.g 48kHz audio is 48000.
   * @param max_length The maximum lenghth of the buffer in milliseconds.
   * @param min_length The minimum age of packets in milliseconds before eligible for dequeue.
   */
  JitterBuffer(std::size_t element_size,
               std::size_t packet_elements,
               std::uint32_t clock_rate,
               std::chrono::milliseconds max_length,
               std::chrono::milliseconds min_length,
               const cantina::LoggerPointer &logger);

  /**
   * @brief Destroy the Jitter Buffer object
   */
  ~JitterBuffer();

  /**
   * @brief Enqueue a number of packets onto the buffer. This must be called from a single writer thread.
   *
   * @param packets The packets to enqueue.
   * @param concealment_callback Fired when concealment data needs to be generated.
   * @returns The number of elements actually enqueued, including concealment.
   */
  std::size_t Enqueue(const std::vector<Packet> &packets, const ConcealmentCallback &concealment_callback);

  /**
   * @brief Dequeue a number of packets into the given destination. This must be called from a single reader thread.
   *
   * @param destination The buffer to copy the data into.
   * @param destination_length Length of destination buffer in bytes.
   * @param elements The number of elements to dequeue.
   * @returns The number of elements actually dequeued.
   */
  std::size_t Dequeue(std::uint8_t *destination, const std::size_t &destination_length, const std::size_t &elements);

  /**
   * @brief Get a read pointer for the buffer at the given packet offset.
   * @param read_offset_elements Offset in packets.
   * @return Pointer into the buffer at the requested offset.
   */
  std::uint8_t *GetReadPointerAtPacketOffset(std::size_t read_offset_elements) const;

  /**
   *
   * @return Current depth of the buffer in milliseconds.
   */
  std::chrono::milliseconds GetCurrentDepth() const;

#ifdef LIBJITTER_BUILD_TESTS
  friend class BufferInspector;
#endif

  public:
  cantina::LoggerPointer logger;

  private:
  std::size_t element_size;
  std::size_t packet_elements;
  std::chrono::milliseconds clock_rate;
  std::chrono::milliseconds min_length;
  std::chrono::milliseconds max_length;

  std::uint8_t *buffer;
  std::size_t read_offset;
  std::size_t write_offset;
  std::size_t max_size_bytes;
  std::atomic<std::size_t> written;
  std::atomic<std::size_t> written_elements;
  std::optional<unsigned long> last_written_sequence_number;
  std::atomic<bool> play;
  void *vm_user_data;
  std::size_t latest_written_elements;
  std::atomic<unsigned long> dont_walk_beyond;

  std::size_t GenerateConcealment(std::size_t packets, const ConcealmentCallback &callback);
  std::size_t Update(const Packet &packet);
  std::size_t CopyIntoBuffer(const Packet &packet);
  std::size_t CopyIntoBuffer(const std::uint8_t *source, std::size_t length, bool manual_increment, std::size_t offset_offset_bytes);
  std::size_t CopyOutOfBuffer(std::uint8_t *destination, std::size_t length, std::size_t required_bytes, bool strict);
  void UnwindRead(std::size_t unwind_bytes);
  void ForwardRead(std::size_t forward_bytes);
  void UnwindWrite(std::size_t unwind_bytes);
  void ForwardWrite(std::size_t forward_bytes);
  [[nodiscard]] static void *MakeVirtualMemory(std::size_t &length, void *user_data);
  static void FreeVirtualMemory(void *address, std::size_t length, void *user_data);
};
