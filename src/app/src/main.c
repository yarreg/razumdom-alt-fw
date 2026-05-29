#include "board.h"
#include "app_config.h"
#include "app_modbus.h"
#include "boot_request.h"
#include "config_registers.h"
#include "buttons.h"
#include "channels.h"
#include "dimming.h"
#include "image.h"
#include "inputs.h"
#include "memory_map.h"
#include "registers.h"
#include "timebase.h"

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/iwdg.h>

__attribute__((section(".image_header"), used)) const struct image_header app_image_header = {
        .magic = FW_IMAGE_MAGIC,
        .header_version = FW_HEADER_VERSION,
        .image_size = 0, /* Filled by release tooling. */
        .image_crc32 = 0,
        .build_version = FW_APP_BUILD_VERSION,
        .flags = 0,
        .device_id = BOARD_DEVICE_ID,
};

static struct app_registers regs;

int main(void) {
    uint32_t last_led_ms = 0u;

    board_clock_setup();
    board_relocate_vector_table(APP_BASE_ADDR);
    timebase_setup();
    board_gpio_safe_setup();

    registers_defaults(&regs);
    app_config_init();
    app_config_load_or_defaults(&regs);
    app_modbus_init(&regs);
    const struct ddm_config *config = config_registers_config();
    inputs_init(config->di_adc_active_threshold, config->debounce_ms);
    dimming_setup();

    /*
     * libopencm3 calculates prescaler/reload assuming LSI = 32 kHz, but
     * STM32F103 LSI is 40 kHz nominal (range 30-60 kHz), so actual timeout =
     * requested x (32 / f_LSI):
     *   30 kHz (min):  500 x 32/30 = ~533 ms
     *   40 kHz (typ):  500 x 32/40 = ~400 ms
     *   60 kHz (max):  500 x 32/60 = ~267 ms
     * Flash config save erases/programs one 1 KiB page, so even at the maximum
     * LSI frequency the watchdog margin remains comfortably above one loop.
     */
    iwdg_set_period_ms(500);
    iwdg_start();

    for (;;) {
        uint32_t now = timebase_ms();
        struct ddm_input_event input_event;
        static enum ddm_config_register_command pending_command = DDM_CONFIG_CMD_NONE;

        app_modbus_poll();
        config = config_registers_config();
        if (pending_command == DDM_CONFIG_CMD_NONE) {
            pending_command = config_registers_take_command();
        }
        if (pending_command != DDM_CONFIG_CMD_NONE && app_modbus_tx_idle()) {
            switch (pending_command) {
            case DDM_CONFIG_CMD_SAVE:
                (void)app_config_save(&regs);
                break;
            case DDM_CONFIG_CMD_RELOAD:
                (void)app_config_reload(&regs);
                break;
            case DDM_CONFIG_CMD_FACTORY_DEFAULTS:
                app_config_factory_defaults(&regs);
                break;
            case DDM_CONFIG_CMD_NONE:
            default:
                break;
            }
            pending_command = DDM_CONFIG_CMD_NONE;
        }
        config = config_registers_config();
        inputs_set_thresholds(config->di_adc_active_threshold, config->debounce_ms);
        inputs_scan(now);
        while (inputs_pop_event(&input_event)) {
            buttons_on_input_event(&input_event, now);
        }
        buttons_tick(now);

        if (regs.bootloader_requested) {
            channels_turn_all_off();
            boot_request_write(regs.slave_address, regs.hr1_port_config);
            iwdg_reset();
            timebase_delay_ms(10);
            scb_reset_system();
        }

        dimming_poll(now);
        if ((uint32_t)(now - last_led_ms) >= 250u) {
            last_led_ms = now;
            board_led_toggle();
        }
        iwdg_reset();
    }
}
