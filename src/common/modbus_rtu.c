#include "modbus_rtu.h"

#include <string.h>

#include "modbus_crc.h"

static void append_crc(uint8_t *frame, size_t len_without_crc) {
    uint16_t crc = modbus_crc16(frame, len_without_crc);
    frame[len_without_crc] = (uint8_t)(crc & 0xffu);
    frame[len_without_crc + 1u] = (uint8_t)(crc >> 8);
}

static int exception_response(uint8_t slave, uint8_t function, uint8_t exception, uint8_t *response, size_t capacity) {
    if (capacity < 5) {
        return MODBUS_ERR_FRAME;
    }
    response[0] = slave;
    response[1] = (uint8_t)(function | 0x80u);
    response[2] = exception;
    append_crc(response, 3);
    return 5;
}

static uint16_t get_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xffu);
}

static int read_holding(uint8_t slave, const uint8_t *request, uint8_t *response, size_t capacity,
                        const struct modbus_register_api *api) {
    uint16_t start = get_be16(&request[2]);
    uint16_t count = get_be16(&request[4]);

    if (count == 0 || count > 125) {
        return exception_response(slave, request[1], MODBUS_EX_ILLEGAL_VALUE, response, capacity);
    }
    if (capacity < (size_t)(5u + count * 2u)) {
        return MODBUS_ERR_FRAME;
    }

    response[0] = slave;
    response[1] = request[1];
    response[2] = (uint8_t)(count * 2u);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t value = 0;
        int rc = api->read_holding(api->ctx, (uint16_t)(start + i), &value);
        if (rc == MODBUS_EX_ILLEGAL_ADDRESS || rc == MODBUS_EX_ILLEGAL_VALUE || rc == MODBUS_EX_SLAVE_DEVICE_FAILURE) {
            return exception_response(slave, request[1], (uint8_t)rc, response, capacity);
        }
        if (rc != 0) {
            return exception_response(slave, request[1], MODBUS_EX_SLAVE_DEVICE_FAILURE, response, capacity);
        }
        put_be16(&response[3u + i * 2u], value);
    }
    size_t len = 3u + count * 2u;
    append_crc(response, len);
    return (int)(len + 2u);
}

static int write_single(uint8_t slave, const uint8_t *request, uint8_t *response, size_t capacity,
                        const struct modbus_register_api *api) {
    uint16_t address = get_be16(&request[2]);
    uint16_t value = get_be16(&request[4]);
    int rc = api->write_holding(api->ctx, address, value);
    if (rc == MODBUS_EX_ILLEGAL_ADDRESS || rc == MODBUS_EX_ILLEGAL_VALUE || rc == MODBUS_EX_SLAVE_DEVICE_FAILURE) {
        return exception_response(slave, request[1], (uint8_t)rc, response, capacity);
    }
    if (rc != 0) {
        return exception_response(slave, request[1], MODBUS_EX_SLAVE_DEVICE_FAILURE, response, capacity);
    }
    if (capacity < 8) {
        return MODBUS_ERR_FRAME;
    }
    memcpy(response, request, 6);
    append_crc(response, 6);
    return 8;
}

static int write_multiple(uint8_t slave, const uint8_t *request, size_t request_len, uint8_t *response, size_t capacity,
                          const struct modbus_register_api *api) {
    uint16_t start = get_be16(&request[2]);
    uint16_t count = get_be16(&request[4]);
    uint8_t byte_count = request[6];

    if (count == 0 || count > 123 || byte_count != count * 2u || request_len != (size_t)(9u + byte_count)) {
        return exception_response(slave, request[1], MODBUS_EX_ILLEGAL_VALUE, response, capacity);
    }
    if (api->before_write_multiple) {
        int rc = api->before_write_multiple(api->ctx, start, count);
        if (rc != 0) {
            return exception_response(slave, request[1], (uint8_t)rc, response, capacity);
        }
    }
    for (uint16_t i = 0; i < count; i++) {
        uint16_t value = get_be16(&request[7u + i * 2u]);
        int rc = api->write_holding(api->ctx, (uint16_t)(start + i), value);
        if (rc == MODBUS_EX_ILLEGAL_ADDRESS || rc == MODBUS_EX_ILLEGAL_VALUE || rc == MODBUS_EX_SLAVE_DEVICE_FAILURE) {
            return exception_response(slave, request[1], (uint8_t)rc, response, capacity);
        }
        if (rc != 0) {
            return exception_response(slave, request[1], MODBUS_EX_SLAVE_DEVICE_FAILURE, response, capacity);
        }
    }
    if (capacity < 8) {
        return MODBUS_ERR_FRAME;
    }
    response[0] = slave;
    response[1] = request[1];
    put_be16(&response[2], start);
    put_be16(&response[4], count);
    append_crc(response, 6);
    return 8;
}

int modbus_rtu_slave_process(uint8_t slave_address, const uint8_t *request, size_t request_len, uint8_t *response,
                             size_t response_capacity, const struct modbus_register_api *api) {
    if (!request || !response || !api || !api->read_holding || !api->write_holding) {
        return MODBUS_ERR_FRAME;
    }
    if (request_len < 4 || request_len > MODBUS_RTU_MAX_ADU) {
        return MODBUS_ERR_FRAME;
    }
    if (request[0] != slave_address && request[0] != 0) {
        return MODBUS_ERR_NO_RESPONSE;
    }
    uint16_t got_crc = (uint16_t)(((uint16_t)request[request_len - 1u] << 8) | request[request_len - 2u]);
    uint16_t calc_crc = modbus_crc16(request, request_len - 2u);
    if (got_crc != calc_crc) {
        return MODBUS_ERR_CRC;
    }
    if (request[0] == 0) {
        return MODBUS_ERR_NO_RESPONSE;
    }

    uint8_t slave = request[0];
    switch (request[1]) {
    case 0x03:
        if (request_len != 8) {
            return MODBUS_ERR_FRAME;
        }
        return read_holding(slave, request, response, response_capacity, api);
    case 0x06:
        if (request_len != 8) {
            return MODBUS_ERR_FRAME;
        }
        return write_single(slave, request, response, response_capacity, api);
    case 0x10:
        if (request_len < 11) {
            return MODBUS_ERR_FRAME;
        }
        return write_multiple(slave, request, request_len, response, response_capacity, api);
    default:
        return exception_response(slave, request[1], MODBUS_EX_ILLEGAL_FUNCTION, response, response_capacity);
    }
}
