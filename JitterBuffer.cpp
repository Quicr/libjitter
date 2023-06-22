#include "JitterBuffer.hh"
#include <algorithm>
#include <iostream>
#include <mach/mach.h>

using namespace std::chrono;

const std::size_t METADATA_SIZE = sizeof(Header);

JitterBuffer::JitterBuffer(std::size_t element_size, std::uint32_t clock_rate, milliseconds max_length, milliseconds min_length)
    : element_size(element_size),
      clock_rate(clock_rate),
      max_length(max_length),
      min_length(min_length) {
  // VM Address trick for automagic wrap around.
  max_size_bytes = round_page(max_length.count() * (clock_rate / 1000) * (element_size + METADATA_SIZE));
  vm_address_t vm_address;
  kern_return_t result = vm_allocate(mach_task_self(), &vm_address, max_size_bytes * 2, VM_FLAGS_ANYWHERE);
  assert(result == ERR_SUCCESS);
  result = vm_deallocate(mach_task_self(), vm_address + max_size_bytes, max_size_bytes);
  assert(result == ERR_SUCCESS);
  vm_address_t virtual_address = vm_address + max_size_bytes;
  vm_prot_t current;
  vm_prot_t max;
  result = vm_remap(mach_task_self(), &virtual_address, max_size_bytes, 0, 0, mach_task_self(), vm_address, 0, &current, &max, VM_INHERIT_DEFAULT);
  assert(result == ERR_SUCCESS);
  assert(virtual_address == vm_address + max_size_bytes);
  buffer = (std::uint8_t *) virtual_address;
  std::cout << "Allocated JitterBuffer with: " << max_size_bytes << " bytes" << std::endl;
}

JitterBuffer::~JitterBuffer() {
  vm_deallocate(mach_task_self(), (vm_address_t) buffer, max_size_bytes * 2);
}

std::size_t JitterBuffer::Enqueue(const std::vector<Packet> &packets, const ConcealmentCallback &concealment_callback, const ConcealmentCallback &free_callback) {
  std::size_t enqueued = 0;

  for (const Packet &packet: packets) {
    if (packet.sequence_number < last_written_sequence_number) {
      // This should be an update for an existing concealment packet.
      // Update it and continue on.
      // TODO: Handle sequence rollover.
      assert(false);
      Update(packet);
      return packet.elements;
    }

    // // TODO: We should check that there's enough space before we bother to ask for concealment packet generation.
    // if (last_written_sequence_number > 0 && packet.sequence_number != last_written_sequence_number) {
    //   const std::size_t missing = packet.sequence_number - last_written_sequence_number - 1;
    //   if (missing > 0) {
    //     std::cout << "Discontinuity detected. Last written was: " << last_written_sequence_number << " this is: " << packet.sequence_number << std::endl;
    //     std::vector<Packet> concealment_packets = std::vector<Packet>(missing);
    //     for (std::size_t sequence_offset = 0; sequence_offset < missing; sequence_offset++) {
    //       concealment_packets[sequence_offset].sequence_number = last_written_sequence_number + sequence_offset + 1;
    //     }
    //     concealment_callback(concealment_packets);
    //     for (const Packet &concealment_packet: concealment_packets) {
    //       assert(concealment_packet.length > 0);
    //       const std::size_t enqueued_elements = CopyIntoBuffer(concealment_packet);
    //       if (enqueued_elements == 0) {
    //         // There's no more space.
    //         break;
    //       }
    //       enqueued += enqueued_elements;
    //       last_written_sequence_number = concealment_packet.sequence_number;
    //     }
    //     free_callback(concealment_packets);
    //   }
    // }

    // Enqueue this packet of real data.
    const std::size_t enqueued_elements = CopyIntoBuffer(packet);
    if (enqueued_elements == 0) {
      // There's no more space.
      break;
    }
    enqueued += enqueued_elements;
    last_written_sequence_number = packet.sequence_number;
  }
  std::cout << "Enqueued: " << enqueued << std::endl;
  return enqueued;
}

std::size_t JitterBuffer::Dequeue(std::uint8_t *destination, const std::size_t &destination_length, const std::size_t &elements) {

  // Check the destination buffer is big enough.
  const std::size_t required_bytes = elements * element_size;
  assert(destination_length >= required_bytes);
  
  std::size_t dequeued_bytes = 0;
  std::size_t destination_offset = 0;
  while (dequeued_bytes < required_bytes) {
    // Check there's space for a header.
    if (written < METADATA_SIZE) {
      std::cout << "Empty" << std::endl;
      return dequeued_bytes / element_size;
    }

    // Get the header.
    Header header;
    const std::size_t copied = CopyOutOfBuffer((std::uint8_t*)&header, METADATA_SIZE, METADATA_SIZE, true);
    assert(copied == METADATA_SIZE);
    std::cout << "[" << header.sequence_number << "] Got" << std::endl;

    // Is this packet of data old enough?
    const std::int64_t now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    const std::int64_t age = now_ms - header.timestamp;
    assert(age >= 0);
    if (age < min_length.count()) {
      // Not old enough. Stop here and rewind pointer back to header for the next read.
      UnwindRead(METADATA_SIZE);
      std::cout << "[" << header.sequence_number << "] not old enough: " << age << std::endl;
      assert(dequeued_bytes % element_size == 0);
      return dequeued_bytes / element_size;
    }

    if (age > max_length.count()) {
      // It's too old, throw this away and run to the next.
      ForwardRead(header.elements * element_size);
      std::cout << "[" << header.sequence_number << "] Too old: " << age << std::endl;
      continue;
    }

    // Get as much real data as we can.
    const std::size_t available_bytes = header.elements * element_size;
    const std::size_t available_or_space = std::min(available_bytes, destination_length - destination_offset);
    const std::size_t to_dequeue = std::min(available_or_space, required_bytes - destination_offset);
    const std::size_t bytes_dequeued = CopyOutOfBuffer(destination + destination_offset, destination_length - destination_offset, to_dequeue, true);
    assert(bytes_dequeued % element_size == 0); // We should only get whole elements.
    destination_offset += bytes_dequeued;
    if (bytes_dequeued > 0 && bytes_dequeued < available_bytes) {
      // We didn't fully empty a packet, update the header to reflect what's left.
      UnwindRead(METADATA_SIZE);
      std::cout << "[" << header.sequence_number << "] Only got: " << bytes_dequeued << "/" << available_bytes << std::endl;
      std::cout << "[" << header.sequence_number << "] Only got elements: " << bytes_dequeued / element_size << "/" << available_bytes / element_size << std::endl;
      const std::size_t remaining_bytes = available_bytes - bytes_dequeued;
      assert(remaining_bytes % element_size == 0); // We should only get whole elements.
      header.elements = remaining_bytes / element_size;
      memcpy(buffer + read_offset, &header, METADATA_SIZE);
    } else {
      std::cout << "[" << header.sequence_number << "] Got whole packet" << std::endl;
    }

    // Otherwise, we read a whole packet and have space for more.
    const std::size_t dequeued_elements = bytes_dequeued / element_size;
    if (dequeued_elements > header.elements) {
      std::cerr << "WTF. Got " << dequeued_elements << " but had " << header.elements << std::endl;
    }
    assert(dequeued_elements <= header.elements); // We should not get more than available.
    dequeued_bytes += bytes_dequeued;
  }

  assert(dequeued_bytes % element_size == 0); // We should only get whole elements.
  const std::size_t dequeued_elements = dequeued_bytes / element_size;
  if (dequeued_elements > elements) {
    std::cout << "More than asked for: " << dequeued_elements << "/" << elements << std::endl;
  }
  assert(dequeued_elements <= elements); // We should not get more than asked for.
  std::cout << "Got " << dequeued_bytes / element_size << "/" << elements << std::endl;
  return dequeued_elements;
}

bool JitterBuffer::Update(const Packet &packet) {
  // Find the offset at which this packet should live.
  std::size_t age_in_sequence = last_written_sequence_number - packet.sequence_number;
  assert(age_in_sequence > 0);
  std::size_t negative_offset = write_offset - age_in_sequence * (element_size + METADATA_SIZE) + METADATA_SIZE;
  memcpy(buffer + negative_offset, packet.data, packet.length);
  return true;
}

std::size_t JitterBuffer::CopyIntoBuffer(const Packet &packet) {
  // As long we're writing whole elements, we can write partial packets.
  const std::int64_t now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  Header header = Header();
  header.timestamp = now_ms;
  header.sequence_number = packet.sequence_number;
  const std::size_t header_offset = write_offset;
  write_offset = (write_offset + METADATA_SIZE) % max_size_bytes;

  const std::size_t enqueued = CopyIntoBuffer((const std::uint8_t *) packet.data, element_size * packet.elements);
  const std::size_t remainder = enqueued % element_size;
  if (remainder > 0) {
    // Unwind any partial data.
    write_offset = (write_offset - remainder) % max_size_bytes;
    written -= remainder;
  }
  const std::size_t enqueued_element_bytes = enqueued - remainder;
  assert(enqueued_element_bytes % element_size == 0); // We should write whole elements.
  header.elements = enqueued_element_bytes / element_size;
  memcpy(buffer + header_offset, &header, METADATA_SIZE);
  return header.elements;
}

std::size_t JitterBuffer::CopyIntoBuffer(const std::uint8_t *src, const std::size_t length) {

  // Ensure we have enough space.
  const std::size_t space = max_size_bytes - written;
  if (length > space) {
    std::cout << "No space! Wanted: " << length << " space: " << space << std::endl;
    return 0;
  }

  // Copy data into the buffer.
  memcpy(buffer + write_offset, src, length);
  write_offset = (write_offset + length) % max_size_bytes;
  written += length;
  if (written > max_size_bytes) {
    std::cout << "Written: " << written << " but max: " << max_size_bytes << std::endl;
  }
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
  read_offset = (read_offset + available) % max_size_bytes;
  written -= available;
  return available;
}

void JitterBuffer::UnwindRead(const std::size_t unwind_bytes) {
  written += unwind_bytes;
  read_offset = (read_offset - unwind_bytes) % max_size_bytes;
}

void JitterBuffer::ForwardRead(const std::size_t forward_bytes) {
  written -= forward_bytes;
  read_offset = (read_offset + forward_bytes) % max_size_bytes;
}