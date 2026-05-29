#include "app_modbus.h"

#include <stddef.h>
#include <string.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

#include "board.h"
#include "ddm_config.h"
#include "modbus_rtu.h"
#include "timebase.h"

#define APP_MODBUS_RX_BUF_SIZE 256u
#define APP_MODBUS_TX_BUF_SIZE 256u

/* USART1 TX is mapped to DMA1 Channel 4 on STM32F103. */
#define MODBUS_TX_DMA      DMA1
#define MODBUS_TX_DMA_CHAN DMA_CHANNEL4

typedef enum {
    TX_IDLE, /* waiting to receive the next frame */
    TX_DMA,  /* DMA is streaming bytes into USART_DR */
    TX_TC,   /* DMA done; waiting for shift register to drain (USART TC flag) */
} tx_state_t;

static struct app_registers *g_regs;
static uint8_t g_rx_buf[APP_MODBUS_RX_BUF_SIZE];
/* g_tx_buf must be a static (non-stack) object: DMA reads it after send_response() returns. */
static uint8_t g_tx_buf[APP_MODBUS_TX_BUF_SIZE];
static volatile uint16_t g_rx_len;
static volatile uint32_t g_last_rx_ms;
static uint16_t g_applied_port_cfg;
static volatile tx_state_t g_tx_state;

static uint32_t baudrate_from_cfg(uint16_t cfg) {
    switch (cfg & 0x0fu) {
    case DDM_MODBUS_BAUD_9600:
        return 9600u;
    case DDM_MODBUS_BAUD_19200:
        return 19200u;
    case DDM_MODBUS_BAUD_38400:
        return 38400u;
    case DDM_MODBUS_BAUD_57600:
        return 57600u;
    case DDM_MODBUS_BAUD_115200:
        return 115200u;
    default:
        return 57600u;
    }
}

static uint32_t parity_from_cfg(uint16_t cfg) {
    switch ((cfg >> 4) & 0x03u) {
    case DDM_MODBUS_PARITY_EVEN:
        return USART_PARITY_EVEN;
    case DDM_MODBUS_PARITY_ODD:
        return USART_PARITY_ODD;
    case DDM_MODBUS_PARITY_NONE:
    default:
        return USART_PARITY_NONE;
    }
}

static uint32_t databits_from_cfg(uint16_t cfg) {
    uint16_t parity_code = (cfg >> 4) & 0x03u;
    return parity_code == DDM_MODBUS_PARITY_NONE ? 8u : 9u;
}

static uint32_t stopbits_from_cfg(uint16_t cfg) {
    uint16_t stop_bits = (uint16_t)(((cfg >> 6) & 0x03u) + 1u);
    return stop_bits == 1u ? USART_STOPBITS_1 : USART_STOPBITS_2;
}

static void tx_abort(void) {
    if (g_tx_state == TX_DMA) {
        dma_disable_channel(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN);
        usart_disable_tx_dma(USART1);
        DMA_IFCR(MODBUS_TX_DMA) = DMA_IFCR_CGIF4; /* clear all CH4 flags */
    }
    board_rs485_set_tx(false);
    g_tx_state = TX_IDLE;
}

static void configure_usart(uint16_t cfg) {
    tx_abort();
    usart_disable(USART1);
    usart_set_baudrate(USART1, baudrate_from_cfg(cfg));
    usart_set_databits(USART1, databits_from_cfg(cfg));
    usart_set_stopbits(USART1, stopbits_from_cfg(cfg));
    usart_set_mode(USART1, USART_MODE_TX_RX);
    usart_set_parity(USART1, parity_from_cfg(cfg));
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);
    usart_enable(USART1);
    usart_enable_rx_interrupt(USART1);

    g_applied_port_cfg = cfg;
}

static int app_read_holding(void *ctx, uint16_t address, uint16_t *value) {
    const struct app_registers *regs = (const struct app_registers *)ctx;
    return registers_read_hr(regs, address, value) ? MODBUS_OK : MODBUS_EX_ILLEGAL_ADDRESS;
}

static int app_write_holding(void *ctx, uint16_t address, uint16_t value) {
    struct app_registers *regs = (struct app_registers *)ctx;
    return registers_write_hr(regs, address, value) ? MODBUS_OK : MODBUS_EX_ILLEGAL_VALUE;
}

static void send_response(const uint8_t *buf, int len) {
    if (len <= 0) {
        return;
    }

    memcpy(g_tx_buf, buf, (size_t)len);
    g_rx_len = 0u;
    board_rs485_set_tx(true);

    dma_channel_reset(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN);
    dma_set_peripheral_address(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN, USART1 + 0x04U); /* USART_DR */
    dma_set_memory_address(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN, (uint32_t)g_tx_buf);
    dma_set_number_of_data(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN, (uint16_t)len);
    dma_set_read_from_memory(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN);
    dma_set_memory_size(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN, DMA_CCR_MSIZE_8BIT);
    dma_set_peripheral_size(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN, DMA_CCR_PSIZE_8BIT);
    dma_enable_memory_increment_mode(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN);
    usart_enable_tx_dma(USART1);
    dma_enable_channel(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN);

    g_tx_state = TX_DMA;
}

static void process_frame(void) {
    uint8_t frame_buf[APP_MODBUS_TX_BUF_SIZE];
    struct modbus_register_api api = {
            .read_holding = app_read_holding,
            .write_holding = app_write_holding,
            .before_write_multiple = 0,
            .ctx = g_regs,
    };
    int len = modbus_rtu_slave_process(g_regs->slave_address, g_rx_buf, g_rx_len, frame_buf, sizeof(frame_buf), &api);
    send_response(frame_buf, len);
}

void app_modbus_init(struct app_registers *registers) {
    g_regs = registers;
    g_rx_len = 0u;
    g_tx_state = TX_IDLE;
    board_rs485_set_tx(false);

    rcc_periph_clock_enable(RCC_DMA1);
    configure_usart(g_regs->hr1_port_config);
    nvic_enable_irq(NVIC_USART1_IRQ);
}

void usart1_isr(void) {
    uint32_t sr = USART_SR(USART1);
    if ((sr & (USART_SR_RXNE | USART_SR_ORE)) == 0u) {
        return;
    }

    uint8_t ch = (uint8_t)usart_recv(USART1);
    if (g_tx_state != TX_IDLE) {
        return;
    }
    if (g_rx_len < sizeof(g_rx_buf)) {
        g_rx_buf[g_rx_len++] = ch;
        g_last_rx_ms = timebase_ms();
    } else {
        g_rx_len = 0u;
    }
}

void app_modbus_poll(void) {
    if (g_regs->hr1_port_config != g_applied_port_cfg) {
        g_rx_len = 0u;
        configure_usart(g_regs->hr1_port_config);
    }

    /* Advance TX state machine; do not process RX while transmitting. */
    if (g_tx_state == TX_DMA) {
        if (DMA_ISR(MODBUS_TX_DMA) & DMA_ISR_TCIF4) {
            DMA_IFCR(MODBUS_TX_DMA) = DMA_IFCR_CGIF4;
            dma_disable_channel(MODBUS_TX_DMA, MODBUS_TX_DMA_CHAN);
            usart_disable_tx_dma(USART1);
            g_tx_state = TX_TC;
        }
        return;
    }
    if (g_tx_state == TX_TC) {
        if ((USART_SR(USART1) & USART_SR_TC) != 0u) {
            board_rs485_set_tx(false);
            g_rx_len = 0u;
            g_tx_state = TX_IDLE;
        }
        return;
    }

    board_rs485_set_tx(false);
    if (g_rx_len != 0u && (uint32_t)(timebase_ms() - g_last_rx_ms) >= 4u) {
        process_frame();
        g_rx_len = 0u;
    }
}

bool app_modbus_tx_idle(void) {
    return g_tx_state == TX_IDLE;
}
