#pragma once

#include <cstddef>

class JitterBuffer;

class BufferInspector {
  public:
      BufferInspector(JitterBuffer* buffer);
      std::size_t GetWritten() const;
      std::size_t GetReadOffset() const;
      std::size_t GetWriteOffset() const;
  private:
      JitterBuffer* buffer;
};
