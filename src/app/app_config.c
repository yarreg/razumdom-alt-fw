#include "app_config.h"

#include <string.h>

#include <libopencm3/stm32/flash.h>

#include "boot_request.h"
#include "config_registers.h"
#include "crc32.h"
#include "ddm_config.h"
#include "memory_map.h"

#define CONFIG_BLOB_MAGIC   0x434d4444u /* "DDMC" */
#define CONFIG_BLOB_VERSION 5u
#define CONFIG_BLOB_VERSION_V4 4u
#define CONFIG_BLOB_V4_LENGTH ((uint16_t)(sizeof(struct ddm_config) - sizeof(uint16_t)))

struct config_blob_header {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t crc32;
};

struct config_blob {
    struct config_blob_header header;
    struct ddm_config config;
};

static uint32_t g_sequence;

typedef char config_blob_fits_page[(sizeof(struct config_blob) <= CONFIG_PAGE_SIZE_BYTES) ? 1 : -1];

static bool flash_ok(void) {
    return (FLASH_SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) == 0u;
}

static bool blob_read_config(const struct config_blob *blob, struct ddm_config *cfg) {
    if (blob->header.magic != CONFIG_BLOB_MAGIC || blob->header.version != CONFIG_BLOB_VERSION
        || blob->header.length != sizeof(blob->config)) {
        if (blob->header.magic != CONFIG_BLOB_MAGIC || blob->header.version != CONFIG_BLOB_VERSION_V4
            || blob->header.length != CONFIG_BLOB_V4_LENGTH) {
            return false;
        }
    }

    if (blob->header.length > sizeof(blob->config)) {
        return false;
    }

    uint32_t crc = crc32_compute((const uint8_t *)&blob->config, blob->header.length);
    if (crc != blob->header.crc32) {
        return false;
    }

    ddm_config_set_defaults(cfg);
    memcpy(cfg, &blob->config, blob->header.length);

    uint16_t status = DDM_CONFIG_STATUS_OK;
    return ddm_config_validate(cfg, &status);
}

static const struct config_blob *select_best_blob(const struct config_blob *a, bool a_ok,
                                                   const struct config_blob *b, bool b_ok) {
    if (a_ok && b_ok) {
        return (b->header.sequence > a->header.sequence) ? b : a;
    }
    if (a_ok) {
        return a;
    }
    if (b_ok) {
        return b;
    }
    return 0;
}

static bool flash_load_config(struct ddm_config *cfg) {
    struct config_blob a;
    struct config_blob b;

    memcpy(&a, (const void *)CONFIG_PAGE0_ADDR, sizeof(a));
    memcpy(&b, (const void *)CONFIG_PAGE1_ADDR, sizeof(b));

    struct ddm_config a_cfg;
    struct ddm_config b_cfg;
    bool a_ok = blob_read_config(&a, &a_cfg);
    bool b_ok = blob_read_config(&b, &b_cfg);
    const struct config_blob *best = select_best_blob(&a, a_ok, &b, b_ok);
    if (best == 0) {
        g_sequence = 0u;
        return false;
    }

    *cfg = best == &a ? a_cfg : b_cfg;
    g_sequence = best->header.sequence;
    return true;
}

static bool write_blob_to_page(uint32_t page_addr, const struct config_blob *blob) {
    const uint8_t *src = (const uint8_t *)blob;

    flash_unlock();
    flash_erase_page(page_addr);
    if (!flash_ok()) {
        flash_lock();
        return false;
    }

    for (size_t i = 0u; i < sizeof(*blob); i += 2u) {
        uint8_t lo = src[i];
        uint8_t hi = (i + 1u < sizeof(*blob)) ? src[i + 1u] : 0xffu;
        flash_program_half_word(page_addr + (uint32_t)i, (uint16_t)lo | ((uint16_t)hi << 8u));
        if (!flash_ok()) {
            flash_lock();
            return false;
        }
    }

    flash_lock();
    return true;
}

static bool flash_write_config(const struct ddm_config *cfg) {
    struct ddm_config current;
    if (flash_load_config(&current) && memcmp(&current, cfg, sizeof(current)) == 0) {
        return true;
    }

    uint16_t status = DDM_CONFIG_STATUS_OK;
    if (!ddm_config_validate(cfg, &status)) {
        return false;
    }

    struct config_blob blob;
    memset(&blob, 0xffu, sizeof(blob));
    blob.config = *cfg;
    blob.header.magic = CONFIG_BLOB_MAGIC;
    blob.header.version = CONFIG_BLOB_VERSION;
    blob.header.length = (uint16_t)sizeof(blob.config);
    blob.header.sequence = g_sequence + 1u;
    blob.header.crc32 = crc32_compute((const uint8_t *)&blob.config, sizeof(blob.config));

    uint32_t page = (blob.header.sequence & 1u) ? CONFIG_PAGE1_ADDR : CONFIG_PAGE0_ADDR;
    if (!write_blob_to_page(page, &blob)) {
        return false;
    }

    g_sequence = blob.header.sequence;
    return true;
}

static void config_from_runtime(const struct app_registers *regs, struct ddm_config *cfg) {
    *cfg = *config_registers_config();
    cfg->modbus_address = regs->slave_address;
    cfg->modbus_port_cfg = regs->hr1_port_config;
}

static void runtime_from_config(struct app_registers *regs, const struct ddm_config *cfg) {
    regs->slave_address = (uint8_t)cfg->modbus_address;
    regs->hr1_port_config = cfg->modbus_port_cfg;
    (void)config_registers_apply_config(cfg);
}

static void apply_boot_request(struct app_registers *regs) {
    uint8_t requested_slave = 0u;
    uint16_t requested_port_cfg = DDM_MODBUS_DEFAULT_PORT_CFG;

    if (!boot_request_read(&requested_slave, &requested_port_cfg)) {
        return;
    }
    if (requested_slave >= 1u && requested_slave <= 247u) {
        regs->slave_address = requested_slave;
    }
    if (ddm_modbus_port_cfg_valid(requested_port_cfg)) {
        regs->hr1_port_config = requested_port_cfg;
    }
}

void app_config_init(void) {
}

void app_config_load_or_defaults(struct app_registers *regs) {
    struct ddm_config cfg;

    if (flash_load_config(&cfg)) {
        runtime_from_config(regs, &cfg);
    } else {
        ddm_config_set_defaults(&cfg);
        runtime_from_config(regs, &cfg);
        apply_boot_request(regs);
    }

    boot_request_clear();
}

bool app_config_save(const struct app_registers *regs) {
    struct ddm_config cfg;

    config_from_runtime(regs, &cfg);
    return flash_write_config(&cfg);
}

bool app_config_reload(struct app_registers *regs) {
    struct ddm_config cfg;

    if (!flash_load_config(&cfg)) {
        return false;
    }

    runtime_from_config(regs, &cfg);
    return true;
}

void app_config_factory_defaults(struct app_registers *regs) {
    struct ddm_config cfg;

    ddm_config_set_defaults(&cfg);
    runtime_from_config(regs, &cfg);
}
