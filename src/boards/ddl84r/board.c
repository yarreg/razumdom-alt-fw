#include "board.h"

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "ddm_types.h"
#include "timebase.h"

/* RS-485 direction pins:
 *   TX enable:  PA8=1, PB11=0
 *   RX/idle:    PA8=0, PB11=1
 */
#define GPIO_RS485_TX_EN GPIO8  /* GPIOA */
#define GPIO_RS485_RX_EN GPIO11 /* GPIOB */

/* DI0 board button: PA0, pull-up, active LOW */
#define GPIO_DI0 GPIO0 /* GPIOA */

/* DI1-DI8 GPIO inputs (pull-up, active LOW):
 *   DI1=PA2, DI2=PA3, DI3=PA4, DI4=PA5,
 *   DI5=PB8, DI6=PB9, DI7=PB13, DI8=PB14
 *
 * This is the logical numbering used by stock DDL84R scenarios. PC13-PC15
 * are separately readable in stock firmware, but are not DI1-DI8.
 */
#define DI_GPIOA_MASK (GPIO2 | GPIO3 | GPIO4 | GPIO5)
#define DI_GPIOB_MASK (GPIO8 | GPIO9 | GPIO13 | GPIO14)

void board_clock_setup(void) {
    /*
     * Stock DDL84R runs directly from the 12 MHz HSE (RCC_CFGR SWS=HSE),
     * with APB1 divided by 2. TIM3 then sees a 12 MHz timer clock, so
     * PSC=120/ARR=1022 yields the same ~97 Hz PWM as stock firmware.
     */
    rcc_osc_on(RCC_HSE);
    rcc_wait_for_osc_ready(RCC_HSE);
    rcc_set_hpre(RCC_CFGR_HPRE_SYSCLK_NODIV);
    rcc_set_ppre1(RCC_CFGR_PPRE1_HCLK_DIV2);
    rcc_set_ppre2(RCC_CFGR_PPRE2_HCLK_NODIV);
    rcc_set_pll_source(RCC_CFGR_PLLSRC_HSE_CLK);
    rcc_set_pll_multiplication_factor(RCC_CFGR_PLLMUL_PLL_CLK_MUL9);
    rcc_osc_on(RCC_PLL);
    rcc_wait_for_osc_ready(RCC_PLL);
    rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_HSECLK);
    while (rcc_system_clock_source() != RCC_CFGR_SWS_SYSCLKSEL_HSECLK) {
    }
    rcc_ahb_frequency = 12000000u;
    rcc_apb1_frequency = 6000000u;
    rcc_apb2_frequency = 12000000u;

    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_USART1);
    rcc_periph_clock_enable(RCC_PWR);
    rcc_periph_clock_enable(RCC_BKP);
}

void board_gpio_safe_setup(void) {
    /* Disable JTAG but keep SWD. DDL84R uses default USART1 pins PA9/PA10. */
    gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON, 0);

    /* LED output drivers are active-low. Keep outputs inactive until TIM3 takes over. */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO6 | GPIO7);
    gpio_set(GPIOA, GPIO6 | GPIO7);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO0 | GPIO1);
    gpio_set(GPIOB, GPIO0 | GPIO1);

    /* USART1 TX = PA9 (AF open-drain), RX = PA10 (pull-up input). */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_OPENDRAIN, GPIO9);
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO10);
    gpio_set(GPIOA, GPIO10);

    /* RS-485 direction pins. */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_RS485_TX_EN);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_RS485_RX_EN);
    board_rs485_set_tx(false);

    /* DI0 board button: PA0 pull-up input. */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO_DI0);
    gpio_set(GPIOA, GPIO_DI0);
}

void board_led_set(bool on) {
    /* DDL84R has no dedicated status LED. */
    (void)on;
}

void board_led_toggle(void) {
    /* DDL84R has no dedicated status LED. */
}

bool board_di0_pressed(void) {
    return gpio_get(GPIOA, GPIO_DI0) == 0u;
}

void board_rs485_set_tx(bool tx_enabled) {
    if (tx_enabled) {
        gpio_set(GPIOA, GPIO_RS485_TX_EN);
        gpio_clear(GPIOB, GPIO_RS485_RX_EN);
    } else {
        gpio_clear(GPIOA, GPIO_RS485_TX_EN);
        gpio_set(GPIOB, GPIO_RS485_RX_EN);
    }
}

void board_relocate_vector_table(uint32_t vector_base) {
    SCB_VTOR = vector_base;
}

void board_di_init(void) {
    /* DI1-DI4: PA2, PA3, PA4, PA5 - pull-up inputs. */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, DI_GPIOA_MASK);
    gpio_set(GPIOA, DI_GPIOA_MASK);

    /* DI5-DI8: PB8, PB9, PB13, PB14 - pull-up inputs. */
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, DI_GPIOB_MASK);
    gpio_set(GPIOB, DI_GPIOB_MASK);
}

void board_di_raw_scan(bool raw_out[], unsigned int count, uint16_t di_adc_threshold) {
    (void)di_adc_threshold; /* DDL84R reads GPIO directly */

    static const uint16_t di_a_masks[] = {
            GPIO2, GPIO3, GPIO4, GPIO5 /* DI1..DI4 */
    };
    static const uint16_t di_b_masks[] = {
            GPIO8, GPIO9, GPIO13, GPIO14 /* DI5..DI8 */
    };

    if (count == 0u) {
        return;
    }
    raw_out[0] = board_di0_pressed(); /* DI0 */

    for (unsigned int i = 0; i < 4u && (i + 1u) < count; i++) {
        raw_out[i + 1u] = (gpio_get(GPIOA, di_a_masks[i]) == 0u);
    }
    for (unsigned int i = 0; i < 4u && (i + 5u) < count; i++) {
        raw_out[i + 5u] = (gpio_get(GPIOB, di_b_masks[i]) == 0u);
    }
}
