BOARD ?= ddm845r
LIBOPENCM3_DIR ?= thirdparty/libopencm3

OPENOCD ?= openocd
OPENOCD_INTERFACE ?= interface/cmsis-dap.cfg
OPENOCD_TARGET ?= target/stm32f1x.cfg

CLANG_FORMAT ?= clang-format
CLANG_FORMAT_IGNORE ?= .clang-format-ignore
APP_BUILD_DIR = build/$(BOARD)/app/$(BUILD_VERSION)

.DEFAULT_GOAL := help

.PHONY: help all libopencm3 bootloader app size clean erase flash-bootloader flash-app format format-check

help:
	@printf '%s\n' \
		'Razumdom alt firmware build' \
		'' \
		'Usage:' \
		'  make BOARD=ddm845r BUILD_VERSION=0x00010005 app' \
		'  make BOARD=ddl84r  BUILD_VERSION=0x00010005 size' \
		'  make BOARD=ddl84r  BUILD_VERSION=0x00010005 flash-app' \
		'  make BOARD=ddl84r flash-bootloader' \
		'  make BOARD=ddl84r erase' \
		'' \
		'Boards:' \
		'  ddm845r  TRIAC dimmer' \
		'  ddl84r   LED PWM dimmer' \
		'' \
		'Important:' \
		'  BUILD_VERSION is required for app/all/size/flash-app.' \
		'  Use an explicit monotonic value, e.g. 0x00010005.'

all: bootloader app

libopencm3:
	$(MAKE) -C $(LIBOPENCM3_DIR) TARGETS=stm32/f1

bootloader: libopencm3
	$(MAKE) -f common.mk IMAGE=bootloader BOARD=$(BOARD) LIBOPENCM3_DIR=$(LIBOPENCM3_DIR)

app: libopencm3
	$(MAKE) -f common.mk IMAGE=app BOARD=$(BOARD) LIBOPENCM3_DIR=$(LIBOPENCM3_DIR) BUILD_VERSION=$(BUILD_VERSION)

size: bootloader app
	arm-none-eabi-size build/$(BOARD)/bootloader/bootloader.elf $(APP_BUILD_DIR)/app.elf

flash-bootloader: bootloader
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "program build/$(BOARD)/bootloader/bootloader.elf verify reset exit"

flash-app: app
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "program $(APP_BUILD_DIR)/app.bin 0x08002000 verify reset exit"

erase:
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "init; reset halt; stm32f1x mass_erase 0; reset; exit"

clean:
	rm -rf build/$(BOARD)

format:
	@find . \( -name '*.c' -o -name '*.h' \) -not -path './.git/*' \
		| sed 's|^\./||' \
		| grep -Evf $(CLANG_FORMAT_IGNORE) \
		| xargs -r $(CLANG_FORMAT) -i

format-check:
	@find . \( -name '*.c' -o -name '*.h' \) -not -path './.git/*' \
		| sed 's|^\./||' \
		| grep -Evf $(CLANG_FORMAT_IGNORE) \
		| xargs -r $(CLANG_FORMAT) --dry-run --Werror
