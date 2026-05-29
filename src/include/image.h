#ifndef DDM_IMAGE_H
#define DDM_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_ID_DDM845R 1u
#define DEVICE_ID_DDL84R  2u

struct image_header {
    uint32_t magic;
    uint32_t header_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t build_version;
    uint32_t flags;
    uint32_t device_id; /* DEVICE_ID_DDM845R or DEVICE_ID_DDL84R */
};

bool image_header_basic_valid(const struct image_header *hdr);
const struct image_header *image_header_at(uint32_t app_base);
bool app_vector_table_valid(uint32_t app_base);
bool app_image_valid(uint32_t app_base);
uint32_t app_image_build_version(uint32_t app_base);
void jump_to_app(uint32_t app_base) __attribute__((noreturn));

#endif
