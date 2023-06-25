#include <doctest/doctest.h>
#include "JitterBuffer.hh"
#include <chrono>
#include <memory>
#include <thread>
#include <iostream>

using namespace std::chrono;

TEST_CASE("libjitter::construct") {
  auto *buffer = new JitterBuffer(2 * 2, 48000, milliseconds(100), milliseconds(20));
  delete (buffer);
}

TEST_CASE("libjitter::enqueue") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto *buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(20));
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
  auto *buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(20));
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
    auto* buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(0));

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

TEST_CASE("libjitter::partial_read") {
    const std::size_t frame_size = 2*2;
    const std::size_t frames_per_packet = 480;
    auto* buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(0));

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

    // Dequeue should get the available 480.
    const std::size_t to_get = 512;
    void* dequeued_data = malloc(frame_size * to_get);
    std::size_t dequeued_frames = buffer->Dequeue(static_cast<std::uint8_t*>(dequeued_data), frame_size * to_get, to_get);
    REQUIRE_EQ(dequeued_frames, frames_per_packet);
    CHECK_EQ(memcmp(dequeued_data, data, frame_size * frames_per_packet), 0);

    // Teardown.
    delete(buffer);
    free(data);
    free(dequeued_data);
}

TEST_CASE("libjitter::runover_read") {
    const std::size_t frame_size = 2*2;
    const std::size_t frames_per_packet = 480;
    auto* buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(0));

    // Enqueue some data.
    std::size_t total_frames = 0;
    std::vector<Packet> packets = std::vector<Packet>();
    std::vector<void*> data_pointers = std::vector<void*>();
    for (std::size_t index = 0; index < 2; index++) {
      Packet packet = Packet();
      void *data = malloc(frame_size * frames_per_packet);
      data_pointers.push_back(data);
      memset(data, index + 1, frame_size * frames_per_packet);
      packet.data = data;
      packet.length = frame_size * frames_per_packet;
      packet.sequence_number = index;
      packet.elements = frames_per_packet;
      total_frames += packet.elements;
      packets.push_back(packet);
    }
    const std::size_t enqueued = buffer->Enqueue(packets,
            [](const std::vector<Packet>& packets){},
            [](const std::vector<Packet>& packets){});
    CHECK_EQ(enqueued, total_frames);

    // Dequeue should get the 512 across the 2 packets.
    const std::size_t to_get = 512;
    void* dequeued_data = malloc(frame_size * to_get);
    std::size_t dequeued_frames = buffer->Dequeue(static_cast<std::uint8_t*>(dequeued_data), frame_size * to_get, to_get);
    REQUIRE_EQ(dequeued_frames, to_get);

    // Should be 480 samples from packet 0, 32 from packet 1.
    const auto* typed = static_cast<const std::uint8_t*>(dequeued_data);
    CHECK_EQ(memcmp(typed, packets[0].data, frame_size * frames_per_packet), 0);
    CHECK_EQ(memcmp(typed + (frame_size * frames_per_packet), packets[1].data, frame_size * (512 - frames_per_packet)), 0);

    // Teardown.
    delete(buffer);
    free(data_pointers[0]);
    free(data_pointers[1]);
    free(dequeued_data);
}

TEST_CASE("libjitter::concealment") {
    const std::size_t frame_size = 2*2;
    const std::size_t frames_per_packet = 480;
    auto* buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(0));

    // Enqueue some data.
    std::size_t total_frames = 0;
    std::vector<Packet> packets = std::vector<Packet>();
    std::vector<void*> data_pointers = std::vector<void*>();
    for (std::size_t index = 0; index < 2; index++) {
      Packet packet = Packet();
      void *data = malloc(frame_size * frames_per_packet);
      data_pointers.push_back(data);
      memset(data, index + 1, frame_size * frames_per_packet);
      packet.data = data;
      packet.length = frame_size * frames_per_packet;
      packet.sequence_number = index == 0 ? 0 : 2;
      packet.elements = frames_per_packet;
      total_frames += packet.elements;
      packets.push_back(packet);
    }

    void* concealment_data = malloc(1920);
    memset(concealment_data, 1, 1920);
    const std::size_t enqueued = buffer->Enqueue(packets,
            [concealment_data](std::vector<Packet>& packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 1);
              packets[0].data = concealment_data;
              packets[0].length = 1920;
              packets[0].elements = 480;
            },
            [concealment_data](const std::vector<Packet>& packets){
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(memcmp(packets[0].data, concealment_data, 1920), 0);
              free(packets[0].data);
            });
    CHECK_EQ(enqueued, total_frames * 3 / 2);
}