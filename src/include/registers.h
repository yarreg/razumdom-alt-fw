#ifndef DDM_REGISTERS_H
#define DDM_REGISTERS_H

#include <stdbool.h>
#include <stdint.h>

#define FW_APP_VERSION       0x0001u
#ifndef FW_APP_BUILD_VERSION
#define FW_APP_BUILD_VERSION 0u
#endif

struct app_registers {
    uint8_t slave_address;
    uint16_t hr1_port_config;
    bool bootloader_requested;
};

void registers_defaults(struct app_registers *regs);
bool registers_read_hr(const struct app_registers *regs, uint16_t hr, uint16_t *value);
bool registers_write_hr(struct app_registers *regs, uint16_t hr, uint16_t value);

#endif
