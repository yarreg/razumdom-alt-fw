#ifndef DDM_FLASH_WRITER_H
#define DDM_FLASH_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool flash_writer_range_valid(uint32_t image_offset, size_t len, uint32_t image_size);
bool flash_writer_erase_app(void);
bool flash_writer_write(uint32_t image_offset, const uint8_t *data, size_t len, uint32_t image_size);

#endif
