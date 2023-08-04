#include <doctest/doctest.h>
#include "JitterBuffer.hh"
#include "BufferInspector.hh"
#include <chrono>
#include <memory>
#include <map>
#include "test_functions.h"
#include <thread>

using namespace std::chrono;

TEST_CASE("libjitter_implementation::enqueue") {
  const std::size_t frame_size = sizeof(int);
  const std::size_t frames_per_packet = 480;
  auto buffer = JitterBuffer(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  auto inspector = BufferInspector(&buffer);

  // Enqueue test packet.
  Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer.Enqueue(
          packets,
          [](const std::vector<Packet> &) {});
  CHECK_EQ(enqueued, packet.elements);

  // Check internals of buffer.
  const std::size_t expected_bytes = packet.elements * frame_size + JitterBuffer::METADATA_SIZE;
  CHECK_EQ(0, memcmp(packet.data, buffer.GetReadPointerAtPacketOffset(0), frame_size * frames_per_packet));
  free(packet.data);
  CHECK_EQ(expected_bytes, inspector.GetWritten());
  CHECK_EQ(0, inspector.GetReadOffset());
  CHECK_EQ(expected_bytes, inspector.GetWriteOffset());
}

TEST_CASE("libjitter_implementation::concealment") {
  const std::size_t frame_size = 4;
  const std::size_t frames_per_packet = 480;
  auto buffer = JitterBuffer(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

  // Enqueue sequence 1.
  Packet sequence1 = makeTestPacket(1, frame_size, frames_per_packet);
  std::vector<Packet> sequence1Packets = std::vector<Packet>();
  sequence1Packets.push_back(sequence1);
  const std::size_t enqueued1 = buffer.Enqueue(
          sequence1Packets,
          [](const std::vector<Packet> &) {
            FAIL("Expected no callback");
          });
  CHECK_EQ(enqueued1, sequence1.elements);

  // Enqueuing sequence 4 should cause 2 and 3 to be generated from concealment and 4 enqueued.
  Packet sequence4 = makeTestPacket(4, frame_size, frames_per_packet);
  std::vector<Packet> sequence4Packets = std::vector<Packet>();
  sequence4Packets.push_back(sequence4);
  std::map<unsigned long, Packet> concealment_packets;
  std::size_t expected_enqueued = sequence4.elements;
  const std::size_t enqueued4 = buffer.Enqueue(
          sequence4Packets,
          [sequence1, sequence4, &concealment_packets, &expected_enqueued, frames_per_packet](std::vector<Packet> &packets) {
            CHECK_EQ(packets.capacity(), sequence4.sequence_number - sequence1.sequence_number - 1);
            unsigned long expected_sequence = sequence1.sequence_number + 1;
            for (auto& packet : packets) {
              CHECK_EQ(expected_sequence, packet.sequence_number);
              expected_sequence++;
              memset(packet.data, packet.sequence_number, packet.elements * frame_size);
              CHECK_EQ(packet.elements * frame_size, frame_size * frames_per_packet);
              CHECK_EQ(packet.elements, frames_per_packet);
              concealment_packets.emplace(packet.sequence_number, packet);
              expected_enqueued += packet.elements;
            }
          });
  CHECK_EQ(enqueued4, expected_enqueued);

  // After this happens, we should see 1,2,3,4 in the buffer.
  CHECK_EQ(0, memcmp(sequence1.data, buffer.GetReadPointerAtPacketOffset(0), frame_size * frames_per_packet));
  CHECK_EQ(0, memcmp(concealment_packets[2].data, buffer.GetReadPointerAtPacketOffset(1), frame_size * frames_per_packet));
  CHECK_EQ(0, memcmp(concealment_packets[3].data, buffer.GetReadPointerAtPacketOffset(2), frame_size * frames_per_packet));
  CHECK_EQ(0, memcmp(sequence4.data, buffer.GetReadPointerAtPacketOffset(3), frame_size * frames_per_packet));
  free(sequence1.data);
  free(sequence4.data);
}

TEST_CASE("libjitter_implementation::update_existing") {

  // Push 1 and 3 to generate 2, then update 2.
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = JitterBuffer(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

  // Push 1.
  {
    Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
    std::vector<Packet> packets = std::vector<Packet>();
    packets.push_back(packet);
    const std::size_t enqueued = buffer.Enqueue(
            packets,
            [](const std::vector<Packet> &) {
              FAIL("Unexpected concealment");
            });
    CHECK_EQ(enqueued, packet.elements);
    free(packet.data);
  }

  // Push 3.
  {
    Packet packet3 = makeTestPacket(3, frame_size, frames_per_packet);
    std::vector<Packet> packets3 = std::vector<Packet>();
    packets3.push_back(packet3);
    std::size_t concealment_enqueue = 0;
    const std::size_t enqueued3 = buffer.Enqueue(
            packets3,
            [&concealment_enqueue](std::vector<Packet> &packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 2);
              memset(packets[0].data, 2, packets[0].elements * frame_size);
              concealment_enqueue += packets[0].elements;
            });
    CHECK_EQ(enqueued3, packet3.elements + concealment_enqueue);
    free(packet3.data);
  }

  // Now update 2.
  Packet updatePacket{};
  {
    updatePacket = makeTestPacket(2, frame_size, frames_per_packet);
    std::vector<Packet> updatePackets = std::vector<Packet>();
    updatePackets.push_back(updatePacket);
    const std::size_t enqueued = buffer.Enqueue(
            updatePackets,
            [](const std::vector<Packet> &) {
              FAIL("Unexpected concealment");
            });
    // FIXME: This fails because of bad backwards search.
    CHECK_EQ(enqueued, updatePacket.elements);
  }

  // Now inspect the buffer to make sure that the correct packet has been updated.
  CHECK(checkPacketInSlot(&buffer, updatePacket, 1));
  free(updatePacket.data);
}

TEST_CASE("libjitter_implementation::checkPacketInSlot") {
  // Push 1 and 3 to generate 2, then update 2.
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = JitterBuffer(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

  Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer.Enqueue(
          packets,
          [](const std::vector<Packet> &) {
            FAIL("Unexpected concealment");
          });
  CHECK_EQ(enqueued, packet.elements);

  Header retrieved{};
  const std::uint8_t* read = buffer.GetReadPointerAtPacketOffset(0);
  memcpy(&retrieved, read - JitterBuffer::METADATA_SIZE, JitterBuffer::METADATA_SIZE);

  // Make sure the header looks good, and the data has been updated.
  CHECK_EQ(retrieved.sequence_number, packet.sequence_number);
  CHECK_EQ(retrieved.elements, packet.elements);
  CHECK_EQ(0, memcmp(read, packet.data, packet.length));
  free(packet.data);
}

TEST_CASE("libjitter_implementation::run") {
  const std::size_t frames_per_packet = 480;
  const std::size_t iterations = 250;
  auto buffer = JitterBuffer(sizeof(std::size_t), frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  std::thread enqueue([&buffer, frames_per_packet](){
    for (std::size_t index = 0; index < iterations; index++) {
      auto packet = Packet {
        .sequence_number = index,
        .data = calloc(1, sizeof(std::size_t)),
        .length = sizeof(std::size_t),
        .elements = frames_per_packet,
      };
      memcpy(packet.data, &index, sizeof(index));
      std::vector<Packet> packets;
      packets.push_back(packet);
      const std::size_t enqueued = buffer.Enqueue(packets, [](const std::vector<Packet>&){ FAIL(""); });
      free(packet.data);
      REQUIRE_EQ(frames_per_packet, enqueued);
      std::this_thread::sleep_for(microseconds(10));
    }
  });

  std::thread dequeue([&buffer, frames_per_packet](){
    for (std::size_t index = 0; index < iterations; index++) {
      auto* destination = static_cast<std::uint8_t*>(calloc(1, sizeof(std::size_t) * frames_per_packet));
      const std::size_t dequeued = buffer.Dequeue(destination, sizeof(std::size_t) * frames_per_packet, frames_per_packet);
      REQUIRE((dequeued == 0 || dequeued == frames_per_packet));
      std::this_thread::sleep_for(microseconds(10));
      free(destination);
    }
  });

  enqueue.join();
  dequeue.join();
}