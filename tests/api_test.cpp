#include <doctest/doctest.h>
#include "JitterBuffer.hh"
#include <chrono>
#include <memory>
#include <map>
#include "test_functions.h"
#include <thread>

using namespace std::chrono;

TEST_CASE("libjitter::construct") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = JitterBuffer(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
}

TEST_CASE("libjitter::enqueue") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = JitterBuffer(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  Packet packet = makeTestPacket(1, frame_size, frames_per_packet);
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer.Enqueue(
          packets,
          [](const std::vector<Packet> &) {},
          [](const std::vector<Packet> &) {});
  CHECK_EQ(enqueued, packet.elements);
  free(packet.data);
}

TEST_CASE("libjitter::dequeue_empty") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  void* destination = calloc(frames_per_packet * frame_size, 1);
  const std::size_t dequeued = buffer->Dequeue(static_cast<std::uint8_t*>(destination), frames_per_packet * frame_size, 480);
  CHECK_EQ(dequeued, 0);
  free(destination);
}

TEST_CASE("libjitter::enqueue_dequeue")
{
  const std::size_t frame_size = 2*2;
  const std::size_t frames_per_packet = 480;
  auto buffer = JitterBuffer(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));

  // Enqueue some data.
  Packet packet = Packet();
  void* data = calloc(frame_size * frames_per_packet, 1);
  memset(data, 1, frame_size * frames_per_packet);
  packet.data = data;
  packet.length = frame_size * frames_per_packet;
  packet.sequence_number = 1;
  packet.elements = frames_per_packet;
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer.Enqueue(packets,
          [](const std::vector<Packet>&){},
          [](const std::vector<Packet>&){});
  CHECK_EQ(enqueued, packet.elements);

  // Dequeue should get this data.
  void* dequeued_data = calloc(frame_size * frames_per_packet, 1);
  std::size_t dequeued_frames = buffer.Dequeue(static_cast<std::uint8_t*>(dequeued_data), frame_size * frames_per_packet, frames_per_packet);
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
  void* data = calloc(frame_size * frames_per_packet, 1);
  memset(data, 1, frame_size * frames_per_packet);
  packet.data = data;
  packet.length = frame_size * frames_per_packet;
  packet.sequence_number = 1;
  packet.elements = frames_per_packet;
  std::vector<Packet> packets = std::vector<Packet>();
  packets.push_back(packet);
  const std::size_t enqueued = buffer->Enqueue(packets,
          [](const std::vector<Packet>&){},
          [](const std::vector<Packet>&){});
  CHECK_EQ(enqueued, packet.elements);

  // Dequeue should get the available 480.
  const std::size_t to_get = 512;
  void* dequeued_data = calloc(frame_size * to_get, 1);
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
    void *data = calloc(frame_size * frames_per_packet, 1);
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
          [](const std::vector<Packet>&){},
          [](const std::vector<Packet>&){});
  CHECK_EQ(enqueued, total_frames);

  // Dequeue should get the 512 across the 2 packets.
  const std::size_t to_get = 512;
  void* dequeued_data = calloc(frame_size * to_get, 1);
  auto* typed = static_cast<std::uint8_t*>(dequeued_data);
  const std::size_t dequeued_frames = buffer->Dequeue(typed, frame_size * to_get, to_get);
  REQUIRE_EQ(dequeued_frames, to_get);

  // Should be 480 samples from packet 0, 32 from packet 1.
  CHECK_EQ(memcmp(typed, packets[0].data, frame_size * frames_per_packet), 0);
  CHECK_EQ(memcmp(typed + (frame_size * frames_per_packet), packets[1].data, frame_size * (to_get - frames_per_packet)), 0);

  // Should be 448 left.
  const std::size_t second_dequeue = buffer->Dequeue(typed, frame_size * to_get, to_get);
  REQUIRE_EQ(second_dequeue, total_frames - dequeued_frames);
  const auto* typed_packet = static_cast<const std::uint8_t*>(packets[1].data);

  // The data that got dequeued should be equal to the second packet + what we read the first time - the size of the first packet.
  const std::size_t second_packet_offset = dequeued_frames - packets[0].elements;
  REQUIRE_EQ(0, memcmp(typed, typed_packet + (second_packet_offset * frame_size), second_dequeue * frame_size));

  // Should get nothing now.
  const std::size_t third_dequeue = buffer->Dequeue(typed, frame_size * to_get, to_get);
  REQUIRE_EQ(0, third_dequeue);

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
          [](const std::vector<Packet> &) {
            FAIL("Expected no callback");
          },
          [](const std::vector<Packet> &) {
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
          [sequence1, sequence4, &concealment_packets, &expected_enqueued](std::vector<Packet> &packets) {
            CHECK_EQ(packets.capacity(), sequence4.sequence_number - sequence1.sequence_number - 1);
            unsigned long expected_sequence = sequence1.sequence_number + 1;
            for (auto& packet : packets) {
              CHECK_EQ(expected_sequence, packet.sequence_number);
              expected_sequence++;
              packet.data = calloc(frame_size * frames_per_packet, 1);
              packet.length = frame_size * frames_per_packet;
              packet.elements = frames_per_packet;
              concealment_packets.emplace(packet.sequence_number, packet);
              expected_enqueued += packet.elements;
            }
          },
          [sequence1, sequence4, &concealment_packets](std::vector<Packet> &packets) {
            CHECK_EQ(packets.capacity(), sequence4.sequence_number - sequence1.sequence_number - 1);
            for (Packet& packet : packets) {
              const Packet created = concealment_packets.at(packet.sequence_number);
              CHECK_EQ(created, packet);
              free(packet.data);
            }
          });
  CHECK_EQ(enqueued4, expected_enqueued);
  free(sequence1.data);
  free(sequence4.data);
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
          [](const std::vector<Packet> &) {},
          [](const std::vector<Packet> &) {});
  free(packet.data);
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
            [](const std::vector<Packet> &) {
              FAIL("Unexpected concealment");
            },
            [](const std::vector<Packet> &) {
              FAIL("Unexpected free");
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
    const std::size_t enqueued3 = buffer->Enqueue(
            packets3,
            [&concealment_enqueue](std::vector<Packet> &packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 2);
              packets[0].data = calloc(packets[0].length, 1);
              concealment_enqueue += packets[0].length / frame_size;
            },
            [](std::vector<Packet> &packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 2);
              free(packets[0].data);
            });
    CHECK_EQ(enqueued3, packet3.elements + concealment_enqueue);
    free(packet3.data);
  }

  // Now update 2.
  {
    Packet updatePacket = makeTestPacket(2, frame_size, frames_per_packet);
    std::vector<Packet> updatePackets = std::vector<Packet>();
    updatePackets.push_back(updatePacket);
    const std::size_t enqueued = buffer->Enqueue(
            updatePackets,
            [](const std::vector<Packet> &) {
              FAIL("Unexpected concealment");
            },
            [](const std::vector<Packet> &) {
              FAIL("Unexpected free");
            });
    CHECK_EQ(enqueued, updatePacket.elements);
    free(updatePacket.data);
  }
}

TEST_CASE("libjitter::update_existing_partial_read") {

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
            [](const std::vector<Packet> &) {
              FAIL("Unexpected concealment");
            },
            [](const std::vector<Packet> &) {
              FAIL("Unexpected free");
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
    const std::size_t enqueued3 = buffer->Enqueue(
            packets3,
            [&concealment_enqueue](std::vector<Packet> &packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 2);
              packets[0].data = calloc(packets[0].length, 1);
              concealment_enqueue += packets[0].length / frame_size;
            },
            [](std::vector<Packet> &packets) {
              CHECK_EQ(packets.capacity(), 1);
              CHECK_EQ(packets[0].sequence_number, 2);
              free(packets[0].data);
            });
    CHECK_EQ(enqueued3, packet3.elements + concealment_enqueue);
    free(packet3.data);
  }

  // Partially read concealment packet 2.
  const std::size_t to_dequeue = frames_per_packet * 1.5f;
  std::uint8_t* dest = reinterpret_cast<std::uint8_t*>(malloc(to_dequeue * frame_size));
  const std::size_t dequeued = buffer->Dequeue(dest, to_dequeue * frame_size, to_dequeue);
  CHECK_EQ(to_dequeue, dequeued);
  free(dest);

  // Now update 2.
  {
    Packet updatePacket = makeTestPacket(2, frame_size, frames_per_packet);
    std::vector<Packet> updatePackets = std::vector<Packet>();
    updatePackets.push_back(updatePacket);
    const std::size_t enqueued = buffer->Enqueue(
            updatePackets,
            [](const std::vector<Packet> &) {
              FAIL("Unexpected concealment");
            },
            [](const std::vector<Packet> &) {
              FAIL("Unexpected free");
            });
    CHECK_EQ(enqueued, updatePacket.elements - (dequeued - frames_per_packet));
    free(updatePacket.data);
  }
}

TEST_CASE("libjitter::fill_buffer") {
  const std::size_t frame_size = 2 * 2;
  const std::size_t frames_per_packet = 480;
  auto buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, 48000, milliseconds(100), milliseconds(0));
  bool enqueued = true;
  std::uint32_t sequence_number = 0;
  while (enqueued) {
    Packet packet = makeTestPacket(sequence_number++, frame_size, frames_per_packet);
    std::vector<Packet> packets = std::vector<Packet>();
    packets.push_back(packet);
    const std::size_t enqueued_this_iteration = buffer->Enqueue(packets, [](const std::vector<Packet> &){}, [](const std::vector<Packet> &){});
    free(packet.data);
    if (enqueued_this_iteration != packet.elements) break;
  }
}

TEST_CASE("libjitter::too_old") {
  const auto max_age = milliseconds(100);
  auto buffer = JitterBuffer(sizeof(std::size_t), 1, 100000, max_age, milliseconds(0));

  Packet old_packet = makeTestPacket(1, sizeof(std::size_t), 1);
  std::vector<Packet> old_packets;
  old_packets.push_back(old_packet);
  std::size_t enqueued = buffer.Enqueue(old_packets, [](const std::vector<Packet>&){}, [](const std::vector<Packet>&){});
  REQUIRE_EQ(1, enqueued);
  std::this_thread::sleep_for(max_age);

  Packet packet = makeTestPacket(2, sizeof(std::size_t), 1);
  std::vector<Packet> packets;
  packets.push_back(packet);
  enqueued = buffer.Enqueue(packets, [](const std::vector<Packet>&){}, [](const std::vector<Packet>&){});
  REQUIRE_EQ(1, enqueued);

  // Now try and dequeue.  We should get the second packet back.
  auto *destination = reinterpret_cast<std::uint8_t*>(calloc(1, sizeof(std::size_t)));
  const std::size_t dequeued = buffer.Dequeue(destination, sizeof(std::size_t), 1);
  REQUIRE_EQ(1, dequeued);
  REQUIRE_NE(0, memcmp(destination, old_packet.data, sizeof(std::size_t)));
  REQUIRE_EQ(0, memcmp(destination, packet.data, sizeof(std::size_t)));
  free(old_packet.data);
  free(packet.data);
  free(destination);
}

TEST_CASE("libjitter::buffer_too_small")
{
  auto buffer = JitterBuffer(1, 1, 100000, milliseconds(100), milliseconds(0));
  void* dest = malloc(1);
  CHECK_THROWS_WITH_AS(buffer.Dequeue(reinterpret_cast<std::uint8_t*>(dest), 1, 2), "Provided buffer too small. Was: 1, need: 2" ,const std::invalid_argument&);
  free(dest);
}

TEST_CASE("libjitter::buffer_too_small")
{
  auto buffer = JitterBuffer(1, 1, 100000, milliseconds(100), milliseconds(0));
  auto packet = Packet {
    .sequence_number = 1,
    .data = nullptr,
    .length = 0,
    .elements = 2,
  };
  std::vector<Packet> packets;
  packets.push_back(packet);
  CHECK_THROWS_WITH_AS(buffer.Enqueue(packets, [](const std::vector<Packet>&){}, [](const std::vector<Packet>&){}),
                       "Supplied packet elements must match declared number of elements. Got: 2, expected: 1",
                       const std::invalid_argument&);
}

// TODO: Test for only dequeing some of packet, then dequeueing the rest.