MEMORY
{
  rom (rx)  : ORIGIN = 0x08000000, LENGTH = 8K
  ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

INCLUDE cortex-m-generic.ld

ASSERT(__exidx_end <= ORIGIN(rom) + LENGTH(rom), "bootloader exceeds 8 KiB flash region")
