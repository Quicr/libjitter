#pragma once

#include "JitterBuffer.hh"
#include <chrono>
#include <memory>
#include <cassert>

static Packet makeTestPacket(const unsigned long sequence_number, const std::size_t frame_size, const std::size_t frames_per_packet, const std::optional<unsigned long> content_override = std::nullopt) {
  assert(frame_size >= sizeof(int));
  Packet packet{};
  packet.sequence_number = sequence_number;
  void *data = calloc(frame_size * frames_per_packet, 1);
  const unsigned long content = content_override.value_or(sequence_number);
  memset(data, static_cast<int>(content), frame_size * frames_per_packet);
  packet.data = data;
  packet.length = frame_size * frames_per_packet;
  packet.elements = frames_per_packet;
  return packet;
}

[[maybe_unused]] static bool checkPacketInSlot(const JitterBuffer* buffer, const Packet& packet, const std::size_t slot) {
  const std::uint8_t *read = buffer->GetReadPointerAtPacketOffset(slot);
  const Header* header = reinterpret_cast<const Header*>(read - JitterBuffer::METADATA_SIZE);
  return packet.sequence_number == header->sequence_number &&
         packet.elements == header->elements &&
         memcmp(packet.data, read, packet.length) == 0;
}