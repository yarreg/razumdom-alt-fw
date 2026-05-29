#include "boot_update.h"

#include <stddef.h>
#include <string.h>

#include <libopencm3/stm32/usart.h>

#include "board.h"
#include "ddm_config.h"
#include "flash_writer.h"
#include "image.h"
#include "memory_map.h"
#include "modbus_crc.h"
#include "modbus_registers.h"
#include "timebase.h"

#define RX_BUF_SIZE        256u
#define BOOTLOADER_VERSION 1u

struct update_state {
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t build_version;
    uint32_t image_offset;
    uint16_t chunk_words;
    uint16_t chunk[HR_CHUNK_MAX_WORDS];
    uint16_t status;
    bool metadata_valid;
    bool erased;
};

static struct update_state update;
static uint8_t slave;
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_len;
static uint32_t last_rx_ms;

static uint32_t baudrate_from_cfg(uint16_t cfg) {
    switch (cfg & 0x0fu) {
    case DDM_MODBUS_BAUD_9600:
        return 9600u;
    case DDM_MODBUS_BAUD_19200:
        return 19200u;
    case DDM_MODBUS_BAUD_38400:
        return 38400u;
    case DDM_MODBUS_BAUD_57600:
        return 57600u;
    case DDM_MODBUS_BAUD_115200:
        return 115200u;
    default:
        return 57600u;
    }
}

static uint32_t parity_from_cfg(uint16_t cfg) {
    switch ((cfg >> 4) & 0x03u) {
    case DDM_MODBUS_PARITY_EVEN:
        return USART_PARITY_EVEN;
    case DDM_MODBUS_PARITY_ODD:
        return USART_PARITY_ODD;
    case DDM_MODBUS_PARITY_NONE:
    default:
        return USART_PARITY_NONE;
    }
}

static uint32_t databits_from_cfg(uint16_t cfg) {
    uint16_t parity_code = (cfg >> 4) & 0x03u;
    return parity_code == DDM_MODBUS_PARITY_NONE ? 8u : 9u;
}

static uint32_t stopbits_from_cfg(uint16_t cfg) {
    uint16_t stop_bits = (uint16_t)(((cfg >> 6) & 0x03u) + 1u);
    return stop_bits == 1u ? USART_STOPBITS_1 : USART_STOPBITS_2;
}

static uint16_t get_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static void put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void update_reset(void) {
    memset(&update, 0, sizeof(update));
    update.status = UPDATE_STATUS_IDLE;
}

static bool range_valid(uint32_t offset, uint32_t len) {
    return flash_writer_range_valid(offset, len, update.image_size);
}

static uint16_t update_erase(void) {
    if (!update.metadata_valid) {
        return UPDATE_STATUS_ERR_SEQUENCE;
    }
    if (update.image_size == 0u || update.image_size > APP_SIZE_BYTES) {
        return UPDATE_STATUS_ERR_SIZE;
    }
    if (!flash_writer_erase_app()) {
        return UPDATE_STATUS_ERR_FLASH;
    }

    update.erased = true;
    return UPDATE_STATUS_READY;
}

static uint16_t update_write_chunk(void) {
    uint8_t bytes[HR_CHUNK_MAX_WORDS * 2u];
    uint32_t len = (uint32_t)update.chunk_words * 2u;

    if (!update.metadata_valid || !update.erased) {
        return UPDATE_STATUS_ERR_SEQUENCE;
    }
    if (update.chunk_words == 0u || update.chunk_words > HR_CHUNK_MAX_WORDS) {
        return UPDATE_STATUS_ERR_BOUNDS;
    }
    if (!range_valid(update.image_offset, len)) {
        return UPDATE_STATUS_ERR_BOUNDS;
    }

    for (uint16_t i = 0; i < update.chunk_words; i++) {
        bytes[i * 2u] = (uint8_t)(update.chunk[i] >> 8);
        bytes[i * 2u + 1u] = (uint8_t)update.chunk[i];
    }

    if (!flash_writer_write(update.image_offset, bytes, len, update.image_size)) {
        return UPDATE_STATUS_ERR_FLASH;
    }

    return UPDATE_STATUS_READY;
}

static uint16_t update_verify(void) {
    const struct image_header *hdr;

    if (!update.metadata_valid) {
        return UPDATE_STATUS_ERR_SEQUENCE;
    }
    if (!app_image_valid(APP_BASE_ADDR)) {
        return UPDATE_STATUS_ERR_IMAGE;
    }

    hdr = image_header_at(APP_BASE_ADDR);
    if (hdr->device_id != BOARD_DEVICE_ID) {
        return UPDATE_STATUS_ERR_IMAGE; /* Firmware is for a different board. */
    }
    if (hdr->image_size != update.image_size) {
        return UPDATE_STATUS_ERR_IMAGE;
    }
    if (hdr->image_crc32 != update.image_crc32) {
        return UPDATE_STATUS_ERR_CRC;
    }
    if (hdr->build_version != update.build_version) {
        return UPDATE_STATUS_ERR_IMAGE;
    }

    return UPDATE_STATUS_VALID;
}

static void run_command(uint16_t value) {
    switch (value) {
    case UPDATE_CMD_IDLE:
        break;
    case UPDATE_CMD_BEGIN:
        if (update.image_size == 0u || update.image_size > APP_SIZE_BYTES) {
            update.status = UPDATE_STATUS_ERR_SIZE;
        } else {
            update.metadata_valid = true;
            update.erased = false;
            update.status = UPDATE_STATUS_READY;
        }
        break;
    case UPDATE_CMD_ERASE:
        update.status = UPDATE_STATUS_ERASING;
        update.status = update_erase();
        break;
    case UPDATE_CMD_WRITE_CHUNK:
        update.status = UPDATE_STATUS_WRITING;
        update.status = update_write_chunk();
        break;
    case UPDATE_CMD_VERIFY:
        update.status = UPDATE_STATUS_VERIFYING;
        update.status = update_verify();
        break;
    case UPDATE_CMD_RUN_APP:
        update.status = app_image_valid(APP_BASE_ADDR) ? UPDATE_STATUS_VALID : UPDATE_STATUS_ERR_IMAGE;
        break;
    case UPDATE_CMD_ABORT:
        update_reset();
        break;
    default:
        update.status = UPDATE_STATUS_ERR_COMMAND;
        break;
    }
}

bool boot_update_read_register(uint16_t reg, uint16_t *value) {
    switch (reg) {
    case HR_FW_VERSION:
        *value = (uint16_t)(app_image_build_version(APP_BASE_ADDR) >> 16);
        return true;
    case HR_BOOT_VERSION:
        *value = BOOTLOADER_VERSION;
        return true;
    case HR_DEVICE_MODE:
        *value = DEVICE_MODE_BOOTLOADER;
        return true;
    case HR_APP_BUILD_HI:
        *value = (uint16_t)(app_image_build_version(APP_BASE_ADDR) >> 16);
        return true;
    case HR_APP_BUILD_LO:
        *value = (uint16_t)app_image_build_version(APP_BASE_ADDR);
        return true;
    case HR_UPDATE_COMMAND:
        *value = UPDATE_CMD_IDLE;
        return true;
    case HR_UPDATE_STATUS:
        *value = update.status;
        return true;
    case HR_IMAGE_SIZE_HI:
        *value = (uint16_t)(update.image_size >> 16);
        return true;
    case HR_IMAGE_SIZE_LO:
        *value = (uint16_t)update.image_size;
        return true;
    case HR_IMAGE_CRC_HI:
        *value = (uint16_t)(update.image_crc32 >> 16);
        return true;
    case HR_IMAGE_CRC_LO:
        *value = (uint16_t)update.image_crc32;
        return true;
    case HR_IMAGE_VERSION_HI:
        *value = (uint16_t)(update.build_version >> 16);
        return true;
    case HR_IMAGE_VERSION_LO:
        *value = (uint16_t)update.build_version;
        return true;
    case HR_IMAGE_OFFSET_HI:
        *value = (uint16_t)(update.image_offset >> 16);
        return true;
    case HR_IMAGE_OFFSET_LO:
        *value = (uint16_t)update.image_offset;
        return true;
    case HR_CHUNK_WORD_COUNT:
        *value = update.chunk_words;
        return true;
    default:
        if (reg >= HR_CHUNK_DATA_BASE && reg <= HR_CHUNK_DATA_END) {
            *value = update.chunk[reg - HR_CHUNK_DATA_BASE];
            return true;
        }
        return false;
    }
}

bool boot_update_write_register(uint16_t reg, uint16_t value) {
    switch (reg) {
    case HR_UPDATE_COMMAND:
        run_command(value);
        return true;
    case HR_IMAGE_SIZE_HI:
        update.image_size = (update.image_size & 0x0000ffffu) | ((uint32_t)value << 16);
        return true;
    case HR_IMAGE_SIZE_LO:
        update.image_size = (update.image_size & 0xffff0000u) | value;
        return true;
    case HR_IMAGE_CRC_HI:
        update.image_crc32 = (update.image_crc32 & 0x0000ffffu) | ((uint32_t)value << 16);
        return true;
    case HR_IMAGE_CRC_LO:
        update.image_crc32 = (update.image_crc32 & 0xffff0000u) | value;
        return true;
    case HR_IMAGE_VERSION_HI:
        update.build_version = (update.build_version & 0x0000ffffu) | ((uint32_t)value << 16);
        return true;
    case HR_IMAGE_VERSION_LO:
        update.build_version = (update.build_version & 0xffff0000u) | value;
        return true;
    case HR_IMAGE_OFFSET_HI:
        update.image_offset = (update.image_offset & 0x0000ffffu) | ((uint32_t)value << 16);
        return true;
    case HR_IMAGE_OFFSET_LO:
        update.image_offset = (update.image_offset & 0xffff0000u) | value;
        return true;
    case HR_CHUNK_WORD_COUNT:
        if (value > HR_CHUNK_MAX_WORDS) {
            update.status = UPDATE_STATUS_ERR_BOUNDS;
            return false;
        }
        update.chunk_words = value;
        return true;
    default:
        if (reg >= HR_CHUNK_DATA_BASE && reg <= HR_CHUNK_DATA_END) {
            update.chunk[reg - HR_CHUNK_DATA_BASE] = value;
            return true;
        }
        return false;
    }
}

static void send_bytes(uint8_t *buf, uint16_t len) {
    uint16_t crc = modbus_crc16(buf, len);
    buf[len++] = (uint8_t)crc;
    buf[len++] = (uint8_t)(crc >> 8);

    board_rs485_set_tx(true);
    for (uint16_t i = 0; i < len; i++) {
        usart_send_blocking(USART1, buf[i]);
    }
    while ((USART_SR(USART1) & USART_SR_TC) == 0u) {
    }
    board_rs485_set_tx(false);
}

static void send_exception(uint8_t fn, uint8_t code) {
    uint8_t tx[5];

    tx[0] = slave;
    tx[1] = fn | 0x80u;
    tx[2] = code;
    send_bytes(tx, 3u);
}

static void handle_read(const uint8_t *req) {
    uint16_t start = get_u16(&req[2]);
    uint16_t count = get_u16(&req[4]);
    uint8_t tx[256];

    if (count == 0u || count > 125u) {
        send_exception(req[1], 3u);
        return;
    }

    tx[0] = slave;
    tx[1] = req[1];
    tx[2] = (uint8_t)(count * 2u);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t value;
        if (!boot_update_read_register(start + i, &value)) {
            send_exception(req[1], 2u);
            return;
        }
        put_u16(&tx[3u + i * 2u], value);
    }
    send_bytes(tx, 3u + count * 2u);
}

static void handle_write_single(const uint8_t *req) {
    uint16_t reg = get_u16(&req[2]);
    uint16_t value = get_u16(&req[4]);
    uint8_t tx[8];

    if (reg == HR_UPDATE_COMMAND && value == UPDATE_CMD_RUN_APP) {
        if (app_image_valid(APP_BASE_ADDR)) {
            memcpy(tx, req, 6u);
            send_bytes(tx, 6u);
            jump_to_app(APP_BASE_ADDR);
        }
        update.status = UPDATE_STATUS_ERR_IMAGE;
    }

    if (!boot_update_write_register(reg, value)) {
        send_exception(req[1], 2u);
        return;
    }

    memcpy(tx, req, 6u);
    send_bytes(tx, 6u);
}

static void handle_write_multiple(const uint8_t *req, uint16_t len) {
    uint16_t start = get_u16(&req[2]);
    uint16_t count = get_u16(&req[4]);
    uint8_t byte_count = req[6];
    uint8_t tx[8];

    if (count == 0u || count > 123u || byte_count != count * 2u || len != (uint16_t)(9u + byte_count)) {
        send_exception(req[1], 3u);
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        if (!boot_update_write_register(start + i, get_u16(&req[7u + i * 2u]))) {
            send_exception(req[1], 2u);
            return;
        }
    }

    tx[0] = slave;
    tx[1] = req[1];
    put_u16(&tx[2], start);
    put_u16(&tx[4], count);
    send_bytes(tx, 6u);
}

static void handle_frame(void) {
    uint16_t got_crc;
    uint16_t calc_crc;

    if (rx_len < 4u || rx_buf[0] != slave) {
        return;
    }

    got_crc = (uint16_t)rx_buf[rx_len - 2u] | ((uint16_t)rx_buf[rx_len - 1u] << 8);
    calc_crc = modbus_crc16(rx_buf, rx_len - 2u);
    if (got_crc != calc_crc) {
        return;
    }

    switch (rx_buf[1]) {
    case 3u:
        if (rx_len == 8u) {
            handle_read(rx_buf);
        }
        break;
    case 6u:
        if (rx_len == 8u) {
            handle_write_single(rx_buf);
        }
        break;
    case 16u:
        handle_write_multiple(rx_buf, rx_len);
        break;
    default:
        send_exception(rx_buf[1], 1u);
        break;
    }
}

void boot_update_init(uint8_t slave_addr, uint16_t port_cfg) {
    slave = slave_addr;
    update_reset();

    usart_set_baudrate(USART1, baudrate_from_cfg(port_cfg));
    usart_set_databits(USART1, databits_from_cfg(port_cfg));
    usart_set_stopbits(USART1, stopbits_from_cfg(port_cfg));
    usart_set_mode(USART1, USART_MODE_TX_RX);
    usart_set_parity(USART1, parity_from_cfg(port_cfg));
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);
    usart_enable(USART1);
}

void boot_update_poll(void) {
    while ((USART_SR(USART1) & (USART_SR_RXNE | USART_SR_ORE)) != 0u) {
        uint8_t ch = (uint8_t)usart_recv(USART1);
        if (rx_len < sizeof(rx_buf)) {
            rx_buf[rx_len++] = ch;
            last_rx_ms = timebase_ms();
        } else {
            rx_len = 0;
        }
    }

    if (rx_len != 0u && (uint32_t)(timebase_ms() - last_rx_ms) >= 4u) {
        handle_frame();
        rx_len = 0;
    }
}
