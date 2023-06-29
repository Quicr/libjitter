#ifndef LIBJITTER_PACKET_H
#define LIBJITTER_PACKET_H

#include <stddef.h>
#include <string.h>

struct Packet {
  unsigned long sequence_number;
  void *data;
  size_t length;
  size_t elements;

#ifdef __cplusplus
  bool operator ==(const Packet& other) const {
    return sequence_number == other.sequence_number &&
           memcmp(data, other.data, length) == 0 &&
           length == other.length &&
           elements == other.elements;
  }
#endif
};

#endif
