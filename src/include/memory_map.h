#ifndef DDM_MEMORY_MAP_H
#define DDM_MEMORY_MAP_H

#include <stdint.h>

#define FLASH_BASE_ADDR 0x08000000u

#if defined(BOARD_DDL84R)
#define FLASH_END_ADDR 0x08008000u /* DDL84R: 32K flash */
#elif defined(BOARD_DDM845R)
#define FLASH_END_ADDR 0x08010000u /* DDM845R: 64K flash */
#else
#error "Unknown or undefined board type!"
#endif

#define BOOT_BASE_ADDR  0x08000000u
#define BOOT_SIZE_BYTES 0x00002000u
#define APP_BASE_ADDR   0x08002000u

#if defined(BOARD_DDL84R)
/* Last 2 KiB (0x08007800..0x08007FFF) are reserved for config pages. */
#define APP_END_ADDR    0x08007800u
#define CONFIG_PAGE0_ADDR 0x08007800u
#define CONFIG_PAGE1_ADDR 0x08007c00u
#elif defined(BOARD_DDM845R)
/* Last 2 KiB (0x0800F800..0x0800FFFF) are reserved for config pages. */
#define APP_END_ADDR    0x0800f800u
#define CONFIG_PAGE0_ADDR 0x0800f800u
#define CONFIG_PAGE1_ADDR 0x0800fc00u
#else
#error "Unknown or undefined board type!"
#endif

#define APP_SIZE_BYTES  (APP_END_ADDR - APP_BASE_ADDR)
#define CONFIG_PAGE_SIZE_BYTES 0x00000400u

#define SRAM_BASE_ADDR 0x20000000u
#define SRAM_END_ADDR  0x20005000u

#define FW_IMAGE_MAGIC         0x44504444u /* "DDPD" */
#define FW_HEADER_VERSION      1u
#define FW_IMAGE_HEADER_OFFSET 0x180u
#define BOOT_REQUEST_MAGIC     0xB007B007u
#define BOOT_REQUEST_ADDR_XOR  0x5a5au

#endif
