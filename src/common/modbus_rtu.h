#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stddef.h>
#include <stdint.h>

#define MODBUS_RTU_MAX_ADU 256u

enum modbus_exception {
    MODBUS_EX_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EX_ILLEGAL_ADDRESS = 0x02,
    MODBUS_EX_ILLEGAL_VALUE = 0x03,
    MODBUS_EX_SLAVE_DEVICE_FAILURE = 0x04,
};

enum modbus_status {
    MODBUS_OK = 0,
    MODBUS_ERR_NO_RESPONSE = -1,
    MODBUS_ERR_CRC = -2,
    MODBUS_ERR_FRAME = -3,
    MODBUS_ERR_EXCEPTION = -4,
};

struct modbus_register_api {
    int (*read_holding)(void *ctx, uint16_t address, uint16_t *value);
    int (*write_holding)(void *ctx, uint16_t address, uint16_t value);
    int (*before_write_multiple)(void *ctx, uint16_t address, uint16_t count);
    void *ctx;
};

uint16_t modbus_crc16(const uint8_t *buf, size_t len);
int modbus_rtu_slave_process(uint8_t slave_address, const uint8_t *request, size_t request_len, uint8_t *response,
                             size_t response_capacity, const struct modbus_register_api *api);

#endif
