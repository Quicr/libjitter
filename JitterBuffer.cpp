#include "JitterBuffer.hh"
#include <algorithm>
#include <iostream>
#include <cassert>
#include <csignal>
#include <type_traits>
#include <sstream>
#ifdef __APPLE__
#include <mach/mach.h>
#elif _GNU_SOURCE
#include <sys/mman.h>
#endif

using namespace std::chrono;

JitterBuffer::JitterBuffer(const std::size_t element_size, const std::size_t packet_elements, const std::uint32_t clock_rate, const milliseconds max_length, const milliseconds min_length)
    : element_size(element_size),
      packet_elements(packet_elements),
      clock_rate(clock_rate),
      min_length(min_length),
      max_length(max_length),
      read_offset(0),
      write_offset(0),
      written(0),
      written_elements(0) {
  // Ensure atomic variables are lock free.
  static_assert(std::is_same<decltype(written), std::atomic<std::size_t>>::value);
  static_assert(std::is_same<decltype(written_elements), std::atomic<std::size_t>>::value);
  static_assert(std::atomic<std::size_t>::is_always_lock_free);

  // VM Address trick for automatic wrap around.
  const std::size_t buffer_size = max_length.count() * (clock_rate / 1000)  * (element_size + METADATA_SIZE);
  max_size_bytes = buffer_size;
#if _GNU_SOURCE
  vm_user_data = calloc(1, sizeof(int));
#endif
  buffer = reinterpret_cast<std::uint8_t *>(MakeVirtualMemory(max_size_bytes, vm_user_data));

  // Done.
  memset(buffer, 0, max_size_bytes);
  last_written_sequence_number.reset();
  std::cout << "Allocated JitterBuffer with: " << max_size_bytes << " bytes" << std::endl;
}

JitterBuffer::~JitterBuffer() {
  FreeVirtualMemory(buffer, max_size_bytes, vm_user_data);
}

std::size_t JitterBuffer::Enqueue(const std::vector<Packet> &packets, const ConcealmentCallback &concealment_callback) {
  std::size_t enqueued = 0;

  for (const Packet &packet: packets) {
    // TODO: Handle sequence rollover.
    if (packet.sequence_number < last_written_sequence_number) {
      // This might be an update for an existing concealment packet.
      // Update it and continue on.
      enqueued += Update(packet);
      continue;
    } else if (last_written_sequence_number.has_value() && packet.sequence_number != last_written_sequence_number) {
     const std::size_t last = last_written_sequence_number.value();
     const std::size_t missing = packet.sequence_number - last - 1;
     if (missing > 0) {
       std::cout << "Discontinuity detected. Last written was: " << last << " this is: " << packet.sequence_number << " need: " << missing << std::endl;
       // Alter missing to be the smallest of the missing packets or what we can currently fit in the buffer.
       const std::size_t space = max_size_bytes - written;
       const std::size_t space_elements = space / (element_size + METADATA_SIZE);
       const std::size_t to_conceal = std::min(missing, space_elements);
       if (missing != to_conceal) {
         std::cout << "Couldn't fit all missing. Asking for: " << to_conceal << "/" << missing << std::endl;
       }
       std::vector<Packet> concealment_packets = std::vector<Packet>(to_conceal);
       for (std::size_t sequence_offset = 0; sequence_offset < to_conceal; sequence_offset++) {
         // We need to write the header for this packet.
         const std::int64_t now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
         Header header = {
           .sequence_number = static_cast<uint32_t>(last + sequence_offset + 1),
           .elements = packet_elements,
           .timestamp = static_cast<uint64_t>(now_ms)
         };
         CopyIntoBuffer(reinterpret_cast<std::uint8_t*>(&header), METADATA_SIZE, true, 0);
         write_offset = (write_offset + METADATA_SIZE) % max_size_bytes;
         const std::size_t length = header.elements * element_size;
         concealment_packets[sequence_offset] = {
          .sequence_number = header.sequence_number,
          .elements = header.elements,
          .length = length,
          .data = buffer + write_offset
         };
         write_offset = (write_offset + length) % max_size_bytes;
       }
       concealment_callback(concealment_packets);

       // Now that we've finished providing data, update values for the reader.
       written += to_conceal * ((packet_elements * element_size) + METADATA_SIZE);
       written_elements += to_conceal * packet_elements;
       last_written_sequence_number = last + to_conceal;
       enqueued += packet_elements * to_conceal;
     }
   }

    // Enqueue this packet of real data.
    if (packet.elements != packet_elements) {
      std::ostringstream message;
      message << "Supplied packet elements must match declared number of elements. Got: " << packet.elements << ", expected: " << packet_elements;
      throw std::invalid_argument(message.str());
    }
    const std::size_t enqueued_elements = CopyIntoBuffer(packet);
    if (enqueued_elements == 0 && packet.elements > 0) {
      // There's no more space.
      std::cout << "Enqueue has no more space. This packet will be lost " << packet.sequence_number << std::endl;
      break;
    }
    enqueued += enqueued_elements;
    last_written_sequence_number = packet.sequence_number;
  }
  return enqueued;
}

std::size_t JitterBuffer::Dequeue(std::uint8_t *destination, const std::size_t &destination_length, const std::size_t &elements) {

  // Check the destination buffer is big enough.
  const std::size_t required_bytes = elements * element_size;
  if (destination_length < required_bytes) {
    std::stringstream message;
    message << "Provided buffer too small. Was: " << destination_length << ", need: " << required_bytes;
    throw std::invalid_argument(message.str());
  }
  
  std::size_t dequeued_bytes = 0;
  std::size_t destination_offset = 0;
  while (dequeued_bytes < required_bytes) {
    // Check there's space for a header.
    if (written < METADATA_SIZE) {
      return dequeued_bytes / element_size;
    }

    // Get the header.
    Header header{};
    [[maybe_unused]] const std::size_t copied = CopyOutOfBuffer((std::uint8_t*)&header, METADATA_SIZE, METADATA_SIZE, true);
    assert(copied == METADATA_SIZE);
    assert(header.elements > 0);
    // Is this packet of data old enough?
    const std::uint64_t now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    const std::uint64_t age = now_ms - header.timestamp;
    assert(age >= 0);
    if (age < static_cast<std::uint64_t>(min_length.count())) {
      // Not old enough. Stop here and rewind pointer back to header for the next read.
      UnwindRead(METADATA_SIZE);
      assert(dequeued_bytes % element_size == 0);
      const std::size_t dequeued_elements = dequeued_bytes / element_size;
      written_elements -= dequeued_elements;
      return dequeued_elements;
    }

    if (age >= static_cast<std::uint64_t>(max_length.count())) {
      // It's too old, throw this away and run to the next.
      assert(header.elements <= packet_elements);
      ForwardRead(header.elements * element_size);
      continue;
    }

    // Get as much real data as we can.
    const std::size_t available_bytes = header.elements * element_size;
    const std::size_t available_or_space = std::min(available_bytes, destination_length - destination_offset);
    const std::size_t remaining_required_bytes = required_bytes - destination_offset;
    const std::size_t to_dequeue = std::min(available_or_space, remaining_required_bytes);
    const std::size_t bytes_dequeued = CopyOutOfBuffer(destination + destination_offset, destination_length - destination_offset, to_dequeue, true);
    assert(bytes_dequeued <= to_dequeue); // We shouldn't get more than we asked for.
    assert(bytes_dequeued > 0); // Because we got a header, we should get *something*.
    assert(bytes_dequeued % element_size == 0); // We should only get whole elements out.
    destination_offset += bytes_dequeued;
    [[maybe_unused]] const std::size_t originally_available = header.elements;
    if (bytes_dequeued < available_bytes) {
      // We didn't fully empty a packet, update the header to reflect what's left.
      UnwindRead(METADATA_SIZE);
      const std::size_t remaining_bytes = available_bytes - bytes_dequeued;
      assert(remaining_bytes % element_size == 0); // We should only get whole elements.
      header.elements = remaining_bytes / element_size;
      assert(header.elements > 0);
      memcpy(buffer + read_offset, &header, METADATA_SIZE);
    }

    // Otherwise, we read a whole packet and have space for more.
    [[maybe_unused]] const std::size_t dequeued_elements = bytes_dequeued / element_size;
    assert(dequeued_elements <= originally_available); // We should not get more than available.
    dequeued_bytes += bytes_dequeued;
  }

  assert(dequeued_bytes % element_size == 0); // We should only get whole elements.
  const std::size_t dequeued_elements = dequeued_bytes / element_size;
  assert(dequeued_elements <= elements); // We should not get more than asked for.
  written_elements -= dequeued_elements;
  return dequeued_elements;
}

std::size_t JitterBuffer::Update(const Packet &packet) {
  const std::size_t offset_packets = 1 + (last_written_sequence_number.value() - packet.sequence_number);
  const std::size_t offset_bytes = offset_packets * ((element_size * packet_elements) + METADATA_SIZE);
  const std::size_t offset_write_offset = (write_offset - offset_bytes) % max_size_bytes;
  Header header{};
  memcpy(&header, buffer + offset_write_offset, METADATA_SIZE);
  if (packet.sequence_number != header.sequence_number) {
    // TODO: What do we do here.
    std::cerr << "Our update estimation seemed wrong. Expected: " << packet.sequence_number << " but got: " << header.sequence_number << std::endl;
    return 0;
  }

  // TODO: Assuming we're at the right place here for now.
  memcpy(buffer + offset_write_offset + METADATA_SIZE, packet.data, header.elements * element_size);
  return header.elements;
}

std::size_t JitterBuffer::CopyIntoBuffer(const Packet &packet) {
  // Prepare to write the header.
  const std::size_t space = max_size_bytes - written;
  if (space < METADATA_SIZE) {
    return 0;
  }
  const std::int64_t now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  Header header = Header();
  header.timestamp = now_ms;
  header.sequence_number = packet.sequence_number;
  const std::size_t header_offset = write_offset;
  const std::size_t enqueued = CopyIntoBuffer(static_cast<std::uint8_t *>(packet.data), element_size * packet.elements, true, METADATA_SIZE);
  if (enqueued == 0) {
    // There was space for 0 frames, so write nothing.
    return 0;
  }
  const std::size_t remainder = enqueued % element_size;
  const std::size_t enqueued_element_bytes = enqueued - remainder;
  assert(enqueued_element_bytes % element_size == 0); // We should write whole elements.
  header.elements = enqueued_element_bytes / element_size;
  assert(header.elements > 0);
  memcpy(buffer + header_offset, &header, METADATA_SIZE);
  ForwardWrite(enqueued_element_bytes + METADATA_SIZE);
  assert(written <= max_size_bytes);
  written_elements += header.elements;
  return header.elements;
}

std::size_t JitterBuffer::CopyIntoBuffer(const std::uint8_t *src, const std::size_t length, const bool manual_increment, const std::size_t offset_offset_bytes) {
  assert(written <= max_size_bytes);

  // Ensure we have enough space.
  const std::size_t space = max_size_bytes - written;
  if (length > space) {
    std::cerr << "No space! Wanted: " << length << " space: " << space << std::endl;
    return 0;
  }

  // Copy data into the buffer.
  const std::size_t offset = (write_offset + offset_offset_bytes) % max_size_bytes;
  memcpy(buffer + offset, src, length);
  if (!manual_increment) ForwardWrite(length);
  assert(written <= max_size_bytes);
  return length;
}

std::size_t JitterBuffer::CopyOutOfBuffer(std::uint8_t *destination, const std::size_t length, const std::size_t required_bytes, const bool strict) {
  if (required_bytes > length) {
    // Destination not big enough.
    std::cerr << "Provided buffer not big enough" << std::endl;
    return -1;
  }

  // How much data is actually available?
  const std::size_t currently_written = written;
  if (strict && required_bytes > currently_written) {
    return 0;
  }

  const std::size_t available = std::min(required_bytes, currently_written);
  if (available == 0) {
    return 0;
  }

  // Copy the available data into the destination buffer.
  memcpy(destination, buffer + read_offset, available);
  ForwardRead(available);
  return available;
}

std::uint8_t* JitterBuffer::GetReadPointerAtPacketOffset(const std::size_t read_offset_packets) const {
  const std::size_t read_offset_bytes = METADATA_SIZE + (read_offset_packets * (METADATA_SIZE + (packet_elements * element_size)));
  if (read_offset_bytes >= max_size_bytes) {
    throw std::runtime_error("Offset cannot be greater than the size of the buffer");
  }
  return buffer + read_offset_bytes;
}

void JitterBuffer::UnwindRead(const std::size_t unwind_bytes) {
  assert(unwind_bytes > 0);
  written += unwind_bytes;
  read_offset = ((read_offset - unwind_bytes) + unwind_bytes * max_size_bytes) % max_size_bytes;
}

void JitterBuffer::ForwardRead(const std::size_t forward_bytes) {
  assert(forward_bytes > 0);
  assert(forward_bytes <= written);
  written -= forward_bytes;
  read_offset = (read_offset + forward_bytes) % max_size_bytes;
}

void JitterBuffer::UnwindWrite(const std::size_t unwind_bytes) {
  assert(unwind_bytes > 0);
  assert(unwind_bytes <= written);
  written -= unwind_bytes;
  write_offset = ((write_offset - unwind_bytes) + unwind_bytes * max_size_bytes) % max_size_bytes;
}

void JitterBuffer::ForwardWrite(const std::size_t forward_bytes) {
  assert(forward_bytes > 0);
  written += forward_bytes;
  write_offset = (write_offset + forward_bytes) % max_size_bytes;
}

milliseconds JitterBuffer::GetCurrentDepth() const {
  const float ms = written_elements * 1000 / clock_rate.count();
  return milliseconds(static_cast<std::int64_t>(ms));
}

void* JitterBuffer::MakeVirtualMemory(std::size_t &length, [[maybe_unused]] void* user_data) {
  // Get buffer length as multiple of page size.
#ifdef __APPLE__
  length = round_page(length);
#elif _GNU_SOURCE
  const int page_size = getpagesize();
  length = length + page_size - (length % page_size);
#endif

  void* address;
#if __APPLE__
  vm_address_t buffer_address;
  [[maybe_unused]] kern_return_t result = vm_allocate(mach_task_self(), &buffer_address, length * 2, VM_FLAGS_ANYWHERE);
  assert(result == ERR_SUCCESS);
  result = vm_deallocate(mach_task_self(), buffer_address + length, length);
  assert(result == ERR_SUCCESS);
  vm_address_t virtual_address = buffer_address + length;
  vm_prot_t current;
  vm_prot_t max;
  result = vm_remap(mach_task_self(), &virtual_address, length, 0, 0, mach_task_self(), buffer_address, 0, &current, &max, VM_INHERIT_DEFAULT);
  assert(result == ERR_SUCCESS);
  assert(virtual_address == buffer_address + length);
  address = reinterpret_cast<void*>(buffer_address);
#elif _GNU_SOURCE
  int fd = memfd_create("buffer", 0);
  memcpy(user_data, &fd, sizeof(fd));
  [[maybe_unused]] int truncated = ftruncate(fd, length);
  assert(truncated == 0);
  address = mmap(nullptr, 2 * length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  mmap(address, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  auto typed_address = reinterpret_cast<std::uint8_t*>(address);
  mmap(reinterpret_cast<void*>(typed_address + length), length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
#else
  throw std::runtime_error("No virtual memory implementation");
#endif
  return address;
}

void JitterBuffer::FreeVirtualMemory(void *address, const std::size_t length, [[maybe_unused]] void* user_data) {
#ifdef __APPLE__
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(address), length * 2);
#elif _GNU_SOURCE
  auto typed_address = reinterpret_cast<std::uint8_t*>(address);
  munmap(typed_address + length, length);
  munmap(address, length);
  close(*reinterpret_cast<int*>(user_data));
  free(user_data);
#else
  throw std::runtime_error("No virtual memory implementation");
#endif
}