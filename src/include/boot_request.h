#ifndef DDM_BOOT_REQUEST_H
#define DDM_BOOT_REQUEST_H

#include <stdbool.h>
#include <stdint.h>

void boot_request_write(uint8_t slave_addr, uint16_t port_cfg);
bool boot_request_read(uint8_t *slave_addr, uint16_t *port_cfg);
void boot_request_clear(void);

#endif
