#include "boot_request.h"

#include <libopencm3/stm32/f1/bkp.h>
#include <libopencm3/stm32/pwr.h>

#include "ddm_config.h"
#include "memory_map.h"

static bool slave_addr_valid(uint8_t slave_addr) {
    return slave_addr >= 1u && slave_addr <= 247u;
}

static void backup_write_enable(void) {
    pwr_disable_backup_domain_write_protect();
}

void boot_request_write(uint8_t slave_addr, uint16_t port_cfg) {
    if (!slave_addr_valid(slave_addr)) {
        slave_addr = 0u;
    }
    if (!ddm_modbus_port_cfg_valid(port_cfg)) {
        port_cfg = DDM_MODBUS_DEFAULT_PORT_CFG;
    }

    backup_write_enable();
    BKP_DR1 = BOOT_REQUEST_MAGIC & 0xffffu;
    BKP_DR2 = (BOOT_REQUEST_MAGIC >> 16) & 0xffffu;
    BKP_DR3 = slave_addr;
    BKP_DR4 = ((uint16_t)slave_addr ^ BOOT_REQUEST_ADDR_XOR) & 0xffffu;
    BKP_DR5 = port_cfg;
    BKP_DR6 = (port_cfg ^ BOOT_REQUEST_ADDR_XOR) & 0xffffu;
}

bool boot_request_read(uint8_t *slave_addr, uint16_t *port_cfg) {
    uint32_t magic = (BKP_DR1 & 0xffffu) | ((BKP_DR2 & 0xffffu) << 16);
    uint8_t addr = (uint8_t)(BKP_DR3 & 0xffu);
    uint16_t addr_check = (uint16_t)(BKP_DR4 & 0xffffu);
    uint16_t cfg = (uint16_t)(BKP_DR5 & 0xffffu);
    uint16_t cfg_check = (uint16_t)(BKP_DR6 & 0xffffu);

    if (magic != BOOT_REQUEST_MAGIC) {
        return false;
    }

    if (slave_addr != 0) {
        if (slave_addr_valid(addr) && addr_check == (uint16_t)((uint16_t)addr ^ BOOT_REQUEST_ADDR_XOR)) {
            *slave_addr = addr;
        } else {
            *slave_addr = 0u;
        }
    }
    if (port_cfg != 0) {
        if (ddm_modbus_port_cfg_valid(cfg) && cfg_check == (uint16_t)(cfg ^ BOOT_REQUEST_ADDR_XOR)) {
            *port_cfg = cfg;
        } else {
            *port_cfg = DDM_MODBUS_DEFAULT_PORT_CFG;
        }
    }
    return true;
}

void boot_request_clear(void) {
    backup_write_enable();
    BKP_DR1 = 0u;
    BKP_DR2 = 0u;
    BKP_DR3 = 0u;
    BKP_DR4 = 0u;
    BKP_DR5 = 0u;
    BKP_DR6 = 0u;
}
