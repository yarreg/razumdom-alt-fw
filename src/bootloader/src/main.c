#include "board.h"
#include "boot_request.h"
#include "boot_update.h"
#include "ddm_config.h"
#include "image.h"
#include "memory_map.h"
#include "timebase.h"

#include <libopencm3/stm32/iwdg.h>

#define DEFAULT_MODBUS_SLAVE 34u

int main(void) {
    uint32_t last_blink = 0;
    uint8_t boot_slave = DEFAULT_MODBUS_SLAVE;
    uint8_t requested_slave = 0u;
    uint16_t boot_port_cfg = DDM_MODBUS_DEFAULT_PORT_CFG;
    uint16_t requested_port_cfg = DDM_MODBUS_DEFAULT_PORT_CFG;

    board_clock_setup();
    timebase_setup();
    board_gpio_safe_setup();
    board_led_set(false);

    bool forced = board_di0_pressed();
    bool requested = boot_request_read(&requested_slave, &requested_port_cfg);
    bool valid = app_image_valid(APP_BASE_ADDR);

    if (!forced && !requested && valid) {
        jump_to_app(APP_BASE_ADDR);
    }

    if (requested && requested_slave >= 1u && requested_slave <= 247u) {
        boot_slave = requested_slave;
        boot_port_cfg = requested_port_cfg;
    }
    /*
     * Start IWDG only when staying in the bootloader for a firmware update.
     * Normal boot (jump_to_app above) exits before this point; the app starts
     * its own IWDG (500 ms) independently after its own initialisation.
     *
     * libopencm3 calculates prescaler/reload assuming LSI = 32 kHz, but
     * STM32F103 LSI is 40 kHz nominal (range 30-60 kHz), so actual timeout =
     * requested x (32 / f_LSI):
     *   30 kHz (min):  4000 x 32/30 = ~4267 ms
     *   40 kHz (typ):  4000 x 32/40 = ~3200 ms
     *   60 kHz (max):  4000 x 32/60 = ~2133 ms
     * Worst-case flash erase is ~880 ms (22 pages x 40 ms), so even at the
     * maximum LSI frequency the margin is > 2x.
     */
    iwdg_set_period_ms(4000);
    iwdg_start();

    boot_update_init(boot_slave, boot_port_cfg);
    for (;;) {
        boot_update_poll();
        iwdg_reset();
        if ((uint32_t)(timebase_ms() - last_blink) >= 500u) {
            last_blink = timebase_ms();
            board_led_toggle();
        }
    }
}
