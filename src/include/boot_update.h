#ifndef DDM_BOOT_UPDATE_H
#define DDM_BOOT_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

void boot_update_init(uint8_t slave_addr, uint16_t port_cfg);
void boot_update_poll(void);
bool boot_update_read_register(uint16_t reg, uint16_t *value);
bool boot_update_write_register(uint16_t reg, uint16_t value);

#endif
