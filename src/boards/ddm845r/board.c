#include "board.h"

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/spi.h>

#include "ddm_types.h"

#define MCP3208_MAX_VALUE 4095u

#define BOARD_GPIO_USART1_TX GPIO6
#define BOARD_GPIO_USART1_RX GPIO7
#define GPIO_RS485_DE        GPIO13
#define GPIO_LED             GPIO12
#define GPIO_DI0             GPIO13

void board_clock_setup(void) {
    rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_HSI_48MHZ]);

    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_USART1);
    rcc_periph_clock_enable(RCC_PWR);
    rcc_periph_clock_enable(RCC_BKP);
}

void board_gpio_safe_setup(void) {
    gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON, AFIO_MAPR_USART1_REMAP | AFIO_MAPR_SPI1_REMAP);

    /* TRIAC outputs start as inactive GPIO outputs until dimming takes over TIM3. */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO6 | GPIO7);
    gpio_clear(GPIOA, GPIO6 | GPIO7);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO0 | GPIO1);
    gpio_clear(GPIOB, GPIO0 | GPIO1);

    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO_DI0);
    gpio_set(GPIOB, GPIO_DI0);

    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, BOARD_GPIO_USART1_TX);
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, BOARD_GPIO_USART1_RX);

    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_RS485_DE);
    board_rs485_set_tx(false);

    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_LED);
    board_led_set(false);
}

void board_led_set(bool on) {
    if (on) {
        gpio_set(GPIOB, GPIO_LED);
    } else {
        gpio_clear(GPIOB, GPIO_LED);
    }
}

void board_led_toggle(void) {
    gpio_toggle(GPIOB, GPIO_LED);
}

bool board_di0_pressed(void) {
    return gpio_get(GPIOB, GPIO_DI0) == 0;
}

void board_rs485_set_tx(bool tx_enabled) {
    if (tx_enabled) {
        gpio_set(GPIOC, GPIO_RS485_DE);
    } else {
        gpio_clear(GPIOC, GPIO_RS485_DE);
    }
}

void board_relocate_vector_table(uint32_t vector_base) {
    SCB_VTOR = vector_base;
}

static uint16_t mcp3208_read(uint8_t channel) {
    uint8_t tx0 = (uint8_t)(0x06u | (channel >> 2));
    uint8_t tx1 = (uint8_t)((channel & 0x03u) << 6);
    uint8_t rx1;
    uint8_t rx2;

    gpio_clear(GPIOA, GPIO8);
    (void)spi_xfer(SPI1, tx0);
    rx1 = (uint8_t)spi_xfer(SPI1, tx1);
    rx2 = (uint8_t)spi_xfer(SPI1, 0x00u);
    gpio_set(GPIOA, GPIO8);

    return (uint16_t)(((uint16_t)(rx1 << 8) & 0x0f00u) | rx2) & MCP3208_MAX_VALUE;
}

void board_di_init(void) {
    rcc_periph_clock_enable(RCC_SPI1);

    /* SPI1 remap is set in board_gpio_safe_setup. Set up CS and SPI pins. */
    gpio_set(GPIOA, GPIO8);
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);

    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO3 | GPIO5);
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO4);

    rcc_periph_reset_pulse(RST_SPI1);
    spi_init_master(SPI1, SPI_CR1_BAUDRATE_FPCLK_DIV_64, SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE, SPI_CR1_CPHA_CLK_TRANSITION_1,
                    SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST);
    spi_enable_software_slave_management(SPI1);
    spi_set_nss_high(SPI1);
    spi_enable(SPI1);
}

void board_di_raw_scan(bool raw_out[], unsigned int count, uint16_t di_adc_threshold) {
    if (count == 0u) {
        return;
    }
    raw_out[0] = board_di0_pressed();
    for (unsigned int ch = 0; ch < DDM_ADC_DI_COUNT && (ch + 1u) < count; ch++) {
        uint16_t sample = mcp3208_read((uint8_t)ch);
        raw_out[ch + 1u] = (sample <= di_adc_threshold);
    }
}
