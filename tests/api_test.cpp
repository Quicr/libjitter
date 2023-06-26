#include <doctest/doctest.h>
#include "JitterBuffer.hh"
#include <chrono>
#include <memory>
#include <map>
#include "test_functions.h"

using namespace std::chrono;

TEST_CASE("libjitter::construct") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
}

TEST_CASE("libjitter::enqueue") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer->Enqueue(
          packets,
          [](const std::vector<Packet> &packets) {},
          [](const std::vector<Packet> &packets) {});
  CHECK_EQ(enqueued, packet.elements);
}

TEST_CASE("libjitter::dequeue_empty") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  void* destination = malloc(frames_per_packet * frame_size);
  const std::size_t dequeued = buffer->Dequeue(static_cast<std::uint8_t*>(destination), frames_per_packet * frame_size, 480);
  CHECK_EQ(dequeued, 0);
  free(destination);
}

TEST_CASE("libjitter::enqueue_dequeue")
{
  const std::size_t frame_size = 2*2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

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
  free(data);
  free(dequeued_data);
}

TEST_CASE("libjitter::partial_read") {
  const std::size_t frame_size = 2*2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

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
  free(data);
  free(dequeued_data);
}

TEST_CASE("libjitter::runover_read") {
  const std::size_t frame_size = 2*2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

  // Enqueue some data.
  std::size_t total_frames = 0;
  std::vector<Packet> packets = std::vector<Packet>();
  std::vector<void*> data_pointers = std::vector<void*>();
  for (std::size_t index = 0; index < 2; index++) {
    Packet packet = Packet();
    void *data = malloc(frame_size * frames_per_packet);
    data_pointers.push_back(data);
    auto incremented = static_cast<int>(index + 1);
    memset(data, incremented, frame_size * frames_per_packet);
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
  free(data_pointers[0]);
  free(data_pointers[1]);
  free(dequeued_data);
}

TEST_CASE("libjitter::concealment") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

  // Enqueue sequence 1.
  Packet sequence1 = makeTestPacket(2, frame_size, frames_per_packet);
  std::vector<Packet> sequence1Packets = std::vector<Packet>();
  sequence1Packets.push_back(sequence1);
  const std::size_t enqueued1 = buffer->Enqueue(
          sequence1Packets,
          [](const std::vector<Packet> &packets) {
            FAIL("Expected no callback");
          },
          [](const std::vector<Packet> &packets) {
            FAIL("Expected no callback");
          });
  CHECK_EQ(enqueued1, sequence1.elements);

  // Enqueue sequence 4.
  Packet sequence4 = makeTestPacket(5, frame_size, frames_per_packet);
  std::vector<Packet> sequence4Packets = std::vector<Packet>();
  sequence4Packets.push_back(sequence4);
  std::map<unsigned long, Packet> concealment_packets;
  std::size_t expected_enqueued = sequence4.elements;
  const std::size_t enqueued4 = buffer->Enqueue(
          sequence4Packets,
          [sequence1, sequence4, frame_size, frames_per_packet, &concealment_packets, &expected_enqueued](std::vector<Packet> &packets) {
            CHECK_EQ(packets.capacity(), sequence4.sequence_number - sequence1.sequence_number - 1);
            unsigned long expected_sequence = sequence1.sequence_number + 1;
            for (auto& packet : packets) {
              CHECK_EQ(expected_sequence, packet.sequence_number);
              expected_sequence++;
              packet.data = malloc(frame_size * frames_per_packet);
              packet.length = frame_size * frames_per_packet;
              packet.elements = frames_per_packet;
              concealment_packets.emplace(packet.sequence_number, packet);
              expected_enqueued += packet.elements;
            }
          },
          [sequence1, sequence4, frame_size, frames_per_packet, &concealment_packets](std::vector<Packet> &packets) {
            CHECK_EQ(packets.capacity(), sequence4.sequence_number - sequence1.sequence_number - 1);
            for (Packet& packet : packets) {
              const Packet created = concealment_packets.at(packet.sequence_number);
              CHECK_EQ(created, packet);
              free(packet.data);
            }
          });
  CHECK_EQ(enqueued4, expected_enqueued);
}

TEST_CASE("libjitter::current_depth") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer->Enqueue(
          packets,
          [](const std::vector<Packet> &packets) {},
          [](const std::vector<Packet> &packets) {});
  CHECK_EQ(enqueued, packet.elements);
  CHECK_EQ(milliseconds(10).count(), buffer->GetCurrentDepth().count());
}

TEST_CASE("libjitter::update_existing") {

  // Push 1 and 3 to generate 2, then update 2.
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

  // Push 1.
  {
    Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
    std::vector<Packet> packets = std::vector<Packet>();
    packets.push_back(packet);
    const std::size_t enqueued = buffer->Enqueue(
            packets,
            [](const std::vector<Packet> &packets) {
              FAIL("Unexpected concealment");
            },
            [](const std::vector<Packet> &packets) {
              FAIL("Unexpected free");
            });
    CHECK_EQ(enqueued, packet.elements);
  }

  // Push 3.
  {
    Packet packet3 = makeTestPacket(3, frame_size, frames_per_packet);
    std::vector<Packet> packets3 = std::vector<Packet>();
    packets3.push_back(packet3);
    std::size_t concealment_enqueue = 0;
    const std::size_t enqueued3 = buffer->Enqueue(
            packets3,
            [&concealment_enqueue](std::vector<Packet> &packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 2);
              packets[0].data = malloc(packets[0].length);
              concealment_enqueue += packets[0].length / frame_size;
            },
            [](std::vector<Packet> &packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 2);
              free(packets[0].data);
            });
    CHECK_EQ(enqueued3, packet3.elements + concealment_enqueue);
  }

  // Now update 2.
  {
    Packet updatePacket = makeTestPacket(2, frame_size, frames_per_packet);
    std::vector<Packet> updatePackets = std::vector<Packet>();
    updatePackets.push_back(updatePacket);
    const std::size_t enqueued = buffer->Enqueue(
            updatePackets,
            [](const std::vector<Packet> &packets) {
              FAIL("Unexpected concealment");
            },
            [](const std::vector<Packet> &packets) {
              FAIL("Unexpected free");
            });
    CHECK_EQ(enqueued, updatePacket.elements);
  }
}