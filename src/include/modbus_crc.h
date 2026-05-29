#ifndef DDM_MODBUS_CRC_H
#define DDM_MODBUS_CRC_H

#include <stddef.h>
#include <stdint.h>

uint16_t modbus_crc16(const uint8_t *data, size_t len);

#endif
