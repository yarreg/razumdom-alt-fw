#include "image.h"

#include <libopencm3/cm3/cortex.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/systick.h>

#include "crc32.h"
#include "memory_map.h"

const struct image_header *image_header_at(uint32_t app_base) {
    return (const struct image_header *)(app_base + FW_IMAGE_HEADER_OFFSET);
}

bool image_header_basic_valid(const struct image_header *hdr) {
    if (hdr->magic != FW_IMAGE_MAGIC) {
        return false;
    }
    if (hdr->header_version != FW_HEADER_VERSION) {
        return false;
    }
    if (hdr->image_size < FW_IMAGE_HEADER_OFFSET + sizeof(struct image_header)) {
        return false;
    }
    if (hdr->image_size > APP_SIZE_BYTES) {
        return false;
    }
    return true;
}

bool app_vector_table_valid(uint32_t app_base) {
    const uint32_t *vectors = (const uint32_t *)app_base;
    uint32_t sp = vectors[0];
    uint32_t reset = vectors[1];

    if (sp < SRAM_BASE_ADDR || sp > SRAM_END_ADDR) {
        return false;
    }
    if (reset < APP_BASE_ADDR || reset >= APP_END_ADDR) {
        return false;
    }
    if ((reset & 1u) == 0) {
        return false;
    }
    return true;
}

bool app_image_valid(uint32_t app_base) {
    if (!app_vector_table_valid(app_base)) {
        return false;
    }
    const struct image_header *hdr = image_header_at(app_base);
    if (!image_header_basic_valid(hdr)) {
        return false;
    }

    uint32_t stored_crc = hdr->image_crc32;
    struct image_header temp = *hdr;
    temp.image_crc32 = 0;

    uint32_t crc = crc32_update(0, (const void *)app_base, FW_IMAGE_HEADER_OFFSET);
    crc = crc32_update(crc, &temp, sizeof(temp));
    const uint8_t *rest = (const uint8_t *)(app_base + FW_IMAGE_HEADER_OFFSET + sizeof(temp));
    crc = crc32_update(crc, rest, hdr->image_size - FW_IMAGE_HEADER_OFFSET - sizeof(temp));
    return crc == stored_crc;
}

uint32_t app_image_build_version(uint32_t app_base) {
    if (!app_image_valid(app_base)) {
        return 0;
    }
    return image_header_at(app_base)->build_version;
}

void jump_to_app(uint32_t app_base) {
    const uint32_t *vectors = (const uint32_t *)app_base;
    uint32_t app_sp = vectors[0];
    uint32_t app_reset = vectors[1];

    cm_disable_interrupts();
    systick_interrupt_disable();
    systick_counter_disable();
    SCB_VTOR = app_base;
    __asm volatile("msr msp, %0\n"
                   "cpsie i\n"
                   "bx %1\n"
                   :
                   : "r"(app_sp), "r"(app_reset)
                   :);
    __builtin_unreachable();
}
