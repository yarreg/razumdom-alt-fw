IMAGE ?= app
BOARD ?= ddm845r
LIBOPENCM3_DIR ?= thirdparty/libopencm3

ifeq ($(IMAGE),bootloader)
BUILD_DIR ?= build/$(BOARD)/$(IMAGE)
else
# Application objects embed BUILD_VERSION in compiled code.
# Separate artifact trees prevent stale objects from carrying an older version.
BUILD_DIR ?= build/$(BOARD)/$(IMAGE)/$(BUILD_VERSION)
endif

PREFIX ?= arm-none-eabi
CC := $(PREFIX)-gcc
OBJCOPY := $(PREFIX)-objcopy
SIZE := $(PREFIX)-size
PYTHON ?= python3

ifeq ($(BOARD),ddm845r)
  BOARD_DEFS := -DBOARD_DDM845R=1 -DBOARD_DEVICE_ID=1
  BOARD_DEVICE_ID := 1
else ifeq ($(BOARD),ddl84r)
  BOARD_DEFS := -DBOARD_DDL84R=1 -DBOARD_DEVICE_ID=2
  BOARD_DEVICE_ID := 2
else
  $(error BOARD must be ddm845r or ddl84r)
endif

CPUFLAGS := -mcpu=cortex-m3 -mthumb
CFLAGS := $(CPUFLAGS) -DSTM32F1 -Os -g3 -std=c11 -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror=implicit-function-declaration \
	-Iinclude -Icommon -I$(LIBOPENCM3_DIR)/include
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS := $(CPUFLAGS) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(IMAGE).map \
	-L$(LIBOPENCM3_DIR)/lib
LDLIBS := -lopencm3_stm32f1 -lc -lnosys

BOARD_COMMON_SRCS_ddm845r := boards/ddm845r/board.c
BOARD_COMMON_SRCS_ddl84r  := boards/ddl84r/board.c
BOARD_COMMON_SRCS := $(BOARD_COMMON_SRCS_$(BOARD))

COMMON_SRCS := \
	common/modbus_rtu.c \
	common/src/boot_request.c \
	$(BOARD_COMMON_SRCS) \
	common/src/crc32.c \
	common/src/image.c \
	common/src/modbus_crc.c \
	common/src/timebase.c

BOARD_APP_SRCS_ddm845r := boards/ddm845r/dimming.c
BOARD_APP_SRCS_ddl84r  := boards/ddl84r/dimming.c

ifeq ($(IMAGE),bootloader)
SRCS := $(COMMON_SRCS) bootloader/src/main.c bootloader/src/boot_update.c bootloader/src/flash_writer.c
LDSCRIPT := linker/$(BOARD)/bootloader.ld
DEFS := -DFW_BOOTLOADER=1 $(BOARD_DEFS)
else ifeq ($(IMAGE),app)
ifeq ($(strip $(BUILD_VERSION)),)
  $(error BUILD_VERSION is required for app images. Example: make BOARD=ddl84r BUILD_VERSION=0x00010005 app)
endif
SRCS := $(COMMON_SRCS) \
	common/ddm_config.c \
	app/app_modbus.c \
	app/app_config.c \
	app/config_registers.c \
	app/buttons.c \
	app/channels.c \
	app/inputs.c \
	app/src/main.c \
	app/src/registers.c \
	$(BOARD_APP_SRCS_$(BOARD))
LDSCRIPT := linker/$(BOARD)/app.ld
DEFS := -DFW_APP=1 -DFW_APP_BUILD_VERSION=$(BUILD_VERSION)u $(BOARD_DEFS)
else
$(error IMAGE must be bootloader or app)
endif

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all
all: $(BUILD_DIR)/$(IMAGE).elf $(BUILD_DIR)/$(IMAGE).bin size

$(BUILD_DIR)/$(IMAGE).elf: $(OBJS) $(LDSCRIPT) common.mk
	$(CC) $(LDFLAGS) -T$(LDSCRIPT) $(OBJS) $(LDLIBS) -o $@

$(BUILD_DIR)/$(IMAGE).bin: $(BUILD_DIR)/$(IMAGE).elf
	$(OBJCOPY) -O binary $< $@
ifeq ($(IMAGE),app)
	$(PYTHON) tools/patch_image.py --build-version $(BUILD_VERSION) --device-id $(BOARD_DEVICE_ID) $@
endif

$(BUILD_DIR)/%.c.o: %.c common.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEFS) -MMD -MP -c $< -o $@

.PHONY: size
size: $(BUILD_DIR)/$(IMAGE).elf
	$(SIZE) $<

-include $(DEPS)
