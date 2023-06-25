#pragma once

#include "JitterBuffer.hh"
#include <chrono>

static Packet makeTestPacket(const unsigned long sequence_number, const std::size_t frame_size, const std::size_t frames_per_packet) {
  Packet packet{};
  packet.sequence_number = sequence_number;
  void *data = malloc(frame_size * frames_per_packet);
  memset(data, static_cast<int>(sequence_number), frame_size * frames_per_packet);
  packet.data = data;
  packet.length = frame_size * frames_per_packet;
  packet.elements = frames_per_packet;
  return packet;
}

static void construct() {
  auto *buffer = new JitterBuffer(2 * 2, 48000, std::chrono::milliseconds(100), std::chrono::milliseconds(20));
  delete (buffer);
}