#include "JitterBuffer.hh"

using namespace std::chrono;

const std::size_t METADATA_SIZE = sizeof(std::int64_t);

JitterBuffer::JitterBuffer(std::size_t element_size, std::uint32_t clock_rate, milliseconds min_length, milliseconds max_length)
    : element_size(element_size),
      clock_rate(clock_rate),
      min_length(min_length) {
        max_size_bytes = max_length.count() * (1/clock_rate) * (element_size + METADATA_SIZE);
        buffer = (std::uint8_t*)malloc(max_size_bytes);
        assert(buffer != nullptr);
}

JitterBuffer::~JitterBuffer() {
    free(buffer);
}

std::size_t JitterBuffer::Enqueue(const std::vector<Packet>& packets, const ConcealmentCallback& concealment_callback)
{
    std::size_t enqueued = 0;
    for (const auto& packet : packets) {
        if (packet.sequence_number < last_written_sequence_number) {
            // This should be an update for an existing concealment packet.
            // Update it and continue on.
            // TODO: Handle sequence rollover.
            Update(packet);
            continue;
        }
        
        const std::size_t missing = packet.sequence_number - last_written_sequence_number - 1;
        const std::vector<Packet> concealment_packets = concealment_callback(missing);
        for (const Packet& concealment_packet : concealment_packets) {
            const bool copied = Copy(concealment_packet);
            if (copied) {
                enqueued++;
            }
        }
        const bool copied = Copy(packet);
        if (copied) {
            enqueued++;
        }
        last_written_sequence_number = packet.sequence_number;
    }
    return enqueued;
}

std::size_t JitterBuffer::Dequeue(void *destination, const std::size_t& destination_length, const std::size_t& elements) {
    
    // Check the buffer is big enough.
    const std::size_t required_bytes = elements * element_size;
    assert(destination_length >= required_bytes);
    
    // Keep track of what's happened.
    std::size_t destination_offset = 0;
    std::size_t elements_dequeued = 0;
    
    // Get some data from the buffer as long as it's old enough.
    std::size_t available = 0;
    std::uint8_t* read_pointer = Front(available);
    for (int element_index = 0; element_index < elements; element_index++)
    {
        if (available < element_size + METADATA_SIZE) {
            // There's no more data.
            break;
        }
        
        // Check the timestamp.
        std::int64_t timestamp;
        memcpy(&timestamp, read_pointer, METADATA_SIZE);
        read_pointer += METADATA_SIZE;
        available -= METADATA_SIZE;
        const milliseconds now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
        if (now_ms - milliseconds(timestamp) < min_length) {
            // Not old enough. Stop here.
            break;
        }
        
        // Otherwise, copy the data out.
        memcpy(destination, read_pointer, element_size);
        read_pointer += element_size;
        available -= element_size;
        destination_offset += element_size;
        elements_dequeued++;
        last_read_sequence_number++;
    }
    return elements_dequeued;
}

std::uint8_t* JitterBuffer::Front(std::size_t &available) const {
    available = written;
    return available > 0 ? buffer + read_offset : nullptr;
}

 std::uint8_t* JitterBuffer::Back(std::size_t &available) const {
   available = max_size_bytes - written;
   return available > 0 ? buffer + write_offset : nullptr;
}

bool JitterBuffer::Copy(const Packet& packet) {
    // Get available space and destination pointer.
    std::size_t available = 0;
    void *destination = Back(available);
    if (available < packet.length + METADATA_SIZE) return false;
    
    // Copy timestamp into buffer.
    const std::int64_t now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    memcpy(destination, &now_ms, METADATA_SIZE);
    write_offset += write_offset + METADATA_SIZE % max_size_bytes;
    // Copy data into buffer.
    memcpy(destination, packet.data, packet.length);

    // Update state.
    write_offset = write_offset + packet.length % max_size_bytes;
    written += packet.length;
}

bool JitterBuffer::Update(const Packet &packet) {
    // Find the offset at which this packet should live.
    std::size_t age_in_sequence = last_written_sequence_number - packet.sequence_number;
    assert(age_in_sequence > 0);
    std::size_t negative_offset = write_offset - age_in_sequence * (element_size + METADATA_SIZE) + METADATA_SIZE;
    memcpy(buffer + negative_offset, packet.data, packet.length);
    return true;
}
