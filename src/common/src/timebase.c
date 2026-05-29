#include "timebase.h"

#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>

static volatile uint32_t g_ms;

void sys_tick_handler(void) {
    g_ms++;
}

void timebase_setup(void) {
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload((rcc_ahb_frequency / 1000u) - 1u);
    systick_counter_enable();
    systick_interrupt_enable();
}

uint32_t timebase_ms(void) {
    return g_ms;
}

void timebase_delay_ms(uint32_t ms) {
    uint32_t start = timebase_ms();
    while ((uint32_t)(timebase_ms() - start) < ms) {
        __asm volatile("wfi");
    }
}
