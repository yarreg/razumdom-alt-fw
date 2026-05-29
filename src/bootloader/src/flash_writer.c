#include "flash_writer.h"

#include <libopencm3/stm32/flash.h>

#include "memory_map.h"

#define STM32F1_FLASH_PAGE_SIZE 1024u

static bool flash_error(void) {
    return (FLASH_SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) != 0u;
}

bool flash_writer_range_valid(uint32_t image_offset, size_t len, uint32_t image_size) {
    if (image_size == 0u || image_size > APP_SIZE_BYTES) {
        return false;
    }
    if (image_offset >= image_size) {
        return false;
    }
    if (len > image_size - image_offset) {
        return false;
    }
    if (image_offset >= APP_SIZE_BYTES) {
        return false;
    }
    if (len > APP_SIZE_BYTES - image_offset) {
        return false;
    }
    return true;
}

bool flash_writer_erase_app(void) {
    flash_unlock();
    for (uint32_t addr = APP_BASE_ADDR; addr < APP_END_ADDR; addr += STM32F1_FLASH_PAGE_SIZE) {
        flash_erase_page(addr);
        if (flash_error()) {
            flash_lock();
            return false;
        }
    }
    flash_lock();
    return true;
}

bool flash_writer_write(uint32_t image_offset, const uint8_t *data, size_t len, uint32_t image_size) {
    if (!flash_writer_range_valid(image_offset, len, image_size)) {
        return false;
    }

    uint32_t addr = APP_BASE_ADDR + image_offset;
    if (addr < APP_BASE_ADDR || addr + len > APP_END_ADDR || (addr & 1u) != 0u) {
        return false;
    }

    flash_unlock();
    for (size_t i = 0; i < len; i += 2u) {
        uint16_t halfword = data[i];
        if (i + 1u < len) {
            halfword |= (uint16_t)data[i + 1u] << 8;
        } else {
            halfword |= 0xff00u;
        }
        flash_program_half_word(addr + i, halfword);
        if (flash_error()) {
            flash_lock();
            return false;
        }
    }
    flash_lock();
    return true;
}
