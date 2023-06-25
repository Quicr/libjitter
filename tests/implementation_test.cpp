#include <doctest/doctest.h>
#include "JitterBuffer.hh"
#include <chrono>
#include <memory>
#include <map>
#include "test_functions.h"

using namespace std::chrono;

TEST_CASE("libjitter_implementation::enqueue") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto *buffer = new JitterBuffer(frame_size, 48000, milliseconds(100), milliseconds(20));

  // Enqueue test packet.
  Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer->Enqueue(
          packets,
          [](const std::vector<Packet> &packets) {},
          [](const std::vector<Packet> &packets) {});
  CHECK_EQ(enqueued, packet.elements);

  // Check internals of buffer.
  CHECK_EQ(0, memcmp(packet.data, buffer->GetReadPointer(0), frame_size * frames_per_packet));


  delete (buffer);
}

TEST_CASE("libjitter_implementation::concealment") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_shared<JitterBuffer>(frame_size, 48000, milliseconds(100), milliseconds(0));

  // Enqueue sequence 1.
  Packet sequence1 = makeTestPacket(1, frame_size, frames_per_packet);
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
  Packet sequence4 = makeTestPacket(4, frame_size, frames_per_packet);
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

  // After this happens, we should see 1,2,3,4 in the buffer.
  CHECK_EQ(0, memcmp(sequence1.data, buffer->GetReadPointer(0), frame_size * frames_per_packet));
  free(sequence1.data);
  free(sequence4.data);
}