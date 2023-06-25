#ifndef LIBJITTER_PACKET_H
#define LIBJITTER_PACKET_H

#include <stddef.h>

struct Packet {
  unsigned long sequence_number;
  void *data;
  size_t length;
  size_t elements;

  bool operator ==(const Packet& other) const {
    return sequence_number == other.sequence_number &&
           data == other.data &&
           length == other.length &&
           elements == other.elements;
  }
};

#endif
