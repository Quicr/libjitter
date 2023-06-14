#ifndef LIBJITTER_PACKET_H
#define LIBJITTER_PACKET_H

#include <stddef.h>

struct Packet {
  unsigned long sequence_number;
  const void *data;
  size_t length;
  size_t elements;
};

#endif
