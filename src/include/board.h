#ifndef DDM_BOARD_H
#define DDM_BOARD_H

#include <stdbool.h>
#include <stdint.h>

enum {
    BOARD_LED_OFF = 0,
    BOARD_LED_ON = 1,
};

void board_clock_setup(void);
void board_gpio_safe_setup(void);
void board_led_set(bool on);
void board_led_toggle(void);
bool board_di0_pressed(void);
void board_rs485_set_tx(bool tx_enabled);
void board_relocate_vector_table(uint32_t vector_base);

/* Digital-input hardware abstraction. */
void board_di_init(void);

/*
 * Scan all digital inputs into raw_out[0..count-1].
 * raw_out[0] = DI0 (board button), raw_out[1..8] = DI1..DI8.
 * Active (pressed / low) = true.
 * di_adc_threshold: active level for ADC-based inputs (DDM845R);
 * ignored on boards with direct GPIO inputs (DDL84R).
 */
void board_di_raw_scan(bool raw_out[], unsigned int count, uint16_t di_adc_threshold);

#endif
