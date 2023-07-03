#include "BufferInspector.hh"
#include "JitterBuffer.hh"

BufferInspector::BufferInspector(JitterBuffer *buffer) {
  this->buffer = buffer;
}

std::size_t BufferInspector::GetWritten() const {
  return this->buffer->written;
}

std::size_t BufferInspector::GetReadOffset() const {
  return this->buffer->read_offset;
}

std::size_t BufferInspector::GetWriteOffset() const {
  return this->buffer->write_offset;
}