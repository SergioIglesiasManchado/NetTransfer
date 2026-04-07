#include "protocol.h"
#include <cstdint>

#define POLYNOMIAL 0xEDB88320

uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;

  uint32_t table[256];

  for (int i = 0; i < 256; i++) {
    uint32_t stored_crc = i;
    for (int j = 0; j < 8; j++) {
      if (stored_crc & 1) {
        stored_crc = stored_crc >> 1;
      } else {
        stored_crc = stored_crc >> 1;
        stored_crc = stored_crc ^ POLYNOMIAL;
      }
    }
    table[i] = stored_crc;
  }

  for (int i = 0; i < len; i++) {
    uint8_t index = (crc ^ data[i]) & 0xFF;
    crc = (crc >> 8) ^ table[index];
  }

  return crc ^ 0xFFFFFFFF;
}
