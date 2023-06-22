#include <doctest/doctest.h>
#include "JitterBuffer.hh"
#include <chrono>
#include <memory>
#include <thread>

using namespace std::chrono;

TEST_CASE("libjitter::construct") {
  JitterBuffer *buffer = new JitterBuffer(2 * 2, 48000, milliseconds(100), milliseconds(20));
  delete (buffer);
}

TEST_CASE("libjitter::enqueue") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  JitterBuffer *buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(20));
  Packet packet = Packet();
  packet.data = malloc(frame_size * frames_per_packet);
  packet.length = frame_size * frames_per_packet;
  packet.sequence_number = 1;
  packet.elements = frames_per_packet;
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer->Enqueue(
          packets,
          [](const std::vector<Packet> &packets) {},
          [](const std::vector<Packet> &packets) {});
  CHECK_EQ(enqueued, packet.elements);
  delete (buffer);
}

TEST_CASE("libjitter::dequeue_empty") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  JitterBuffer *buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(20));
  void* destination = malloc(frames_per_packet * frame_size);
  const std::size_t dequeued = buffer->Dequeue(static_cast<std::uint8_t*>(destination), frames_per_packet * frame_size, 480);
  CHECK_EQ(dequeued, 0);
  delete(buffer);
  free(destination);
}

TEST_CASE("libjitter::enqueue_dequeue")
{
    const std::size_t frame_size = 2*2;
    const std::size_t frames_per_packet = 480;
    JitterBuffer* buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(20));

    // Enqueue some data.
    Packet packet = Packet();
    void* data = malloc(frame_size * frames_per_packet);
    memset(data, 1, frame_size * frames_per_packet);
    packet.data = data;
    packet.length = frame_size * frames_per_packet;
    packet.sequence_number = 1;
    packet.elements = frames_per_packet;
    std::vector<Packet> packets = std::vector<Packet>();
    packets.push_back(packet);
    const std::size_t enqueued = buffer->Enqueue(packets,
                                                 [](const std::vector<Packet>& packets){},
                                                 [](const std::vector<Packet>& packets){});
    CHECK_EQ(enqueued, packet.elements);

    // Wait the mininum time.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Dequeue should get this data.
    void* dequeued_data = malloc(frame_size * frames_per_packet);
    std::size_t dequeued_frames = buffer->Dequeue(static_cast<std::uint8_t*>(dequeued_data), frame_size * frames_per_packet, frames_per_packet);
    REQUIRE_EQ(dequeued_frames, frames_per_packet);
    CHECK_EQ(memcmp(dequeued_data, data, frame_size * frames_per_packet), 0);

    // Teardown.
    delete(buffer);
    free(data);
    free(dequeued_data);
}