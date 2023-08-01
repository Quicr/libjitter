#include <JitterBuffer.hh>
#include <benchmark/benchmark.h>
#include <cassert>
#include <iostream>
#include <memory>

std::unique_ptr<JitterBuffer> buffer;
void *data;
const std::size_t frame_size = 1;
const std::size_t frames_per_packet = 480;

static void DoSetup(const benchmark::State &state) {
  const std::size_t sample_rate = 48000;
  const std::chrono::milliseconds max_time = std::chrono::milliseconds(10000);
  const std::chrono::milliseconds min_time = std::chrono::milliseconds(0);
  buffer = std::make_unique<JitterBuffer>(frame_size, frames_per_packet, sample_rate, max_time, min_time);
  data = malloc(frame_size * frames_per_packet);
}

static void DoTeardown(const benchmark::State &state) {
  buffer.reset();
  free(data);
}

static void libjitter_enqueue(benchmark::State &state) {
  std::size_t sequence_number = 0;
  for (auto _: state) {
    auto packet = Packet{
            .data = data,
            .elements = frames_per_packet,
            .length = frame_size * frames_per_packet,
            .sequence_number = sequence_number++};
    std::vector<Packet> packets = std::vector<Packet>();
    packets.push_back(packet);
    const std::size_t enqueued = buffer->Enqueue(
            packets,
            [](const std::vector<Packet> &) {
              assert(false);
            },
            [](const std::vector<Packet> &) {
              assert(false);
            });
    if (enqueued == 0) {
      state.SkipWithMessage("Full");
      break;
    }
  }
}
BENCHMARK(libjitter_enqueue)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(1500);

static void libjitter_concealment(benchmark::State &state) {
  std::size_t sequence_number = 0;

  for (auto _: state) {
    auto packet = Packet{
            .data = data,
            .elements = frames_per_packet,
            .length = frame_size * frames_per_packet,
            .sequence_number = ++sequence_number};
    std::vector<Packet> packets = std::vector<Packet>();
    packets.push_back(packet);
    const std::size_t enqueued = buffer->Enqueue(
            packets,
            [](const std::vector<Packet> &) {
              assert(false);
            },
            [](const std::vector<Packet> &) {
              assert(false);
            });
    if (enqueued == 0) {
      state.SkipWithMessage("Full");
      break;
    }
    sequence_number += state.range(0);
    auto next = Packet{
            .data = data,
            .elements = frames_per_packet,
            .length = frame_size * frames_per_packet,
            .sequence_number = sequence_number};
    std::vector<Packet> nexts = std::vector<Packet>();
    nexts.push_back(next);
    const std::size_t concealed = buffer->Enqueue(
            nexts,
            [](std::vector<Packet> &packets) {
              for (Packet &packet: packets) {
                packet.data = malloc(packet.length);
              }
            },
            [](std::vector<Packet> &packets) {
              for (Packet &packet: packets) {
                free(packet.data);
              }
            });
    if (concealed == 0) {
      state.SkipWithMessage("Full");
      break;
    }
  }
}
BENCHMARK(libjitter_concealment)->DenseRange(1, 20, 1)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(1000);