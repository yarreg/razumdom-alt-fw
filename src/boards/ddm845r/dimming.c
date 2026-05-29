#include "dimming.h"

#include <stdbool.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "ddm_config.h"
#include "timebase.h"

#define DIMMING_MAX_LEVEL             1023u
#define DIMMING_TIMER_STEPS           1023u
#define TRIAC_INITIAL_TIMER_SPAN_US  10500u
#define TRIAC_ZC_GUARD_US              500u
#define TRIAC_VALID_PERIOD_MIN_US     7000u
#define TRIAC_VALID_PERIOD_MAX_US    12000u
#define TRIAC_RETUNE_THRESHOLD_US      100u
#define GPIO_AUX_CONTROL               GPIO5

static volatile uint16_t g_shadow_level[4];
static uint16_t g_target_level[4];
static uint16_t g_applied_level[4];
static uint16_t g_fade_from_level[4];
static uint16_t g_zone_min[4];
static uint16_t g_zone_max[4];
static uint16_t g_output_curve[4];
static uint32_t g_fade_started_ms[4];
static uint16_t g_fade_duration_ms[4];
static uint16_t g_measured_half_period_us;
static uint16_t g_timer_span_us;
static bool g_fade_active[4];
static bool g_have_zc_sample;

static uint16_t curve_level(uint16_t level, uint16_t curve) {
    uint32_t x = level > DIMMING_MAX_LEVEL ? DIMMING_MAX_LEVEL : level;
    uint32_t x2 = (x * x + DIMMING_MAX_LEVEL / 2u) / DIMMING_MAX_LEVEL;
    uint32_t x3 = (x2 * x + DIMMING_MAX_LEVEL / 2u) / DIMMING_MAX_LEVEL;

    switch (curve) {
    case DDM_OUTPUT_CURVE_GAMMA_0_5:
    case DDM_OUTPUT_CURVE_GAMMA_0_7: {
        uint32_t lo = 0u;
        uint32_t hi = DIMMING_MAX_LEVEL;
        uint32_t sqrt_x = 0u;

        while (lo <= hi) {
            uint32_t mid = (lo + hi) / 2u;
            uint32_t mid2 = (mid * mid + DIMMING_MAX_LEVEL / 2u) / DIMMING_MAX_LEVEL;
            if (mid2 <= x) {
                sqrt_x = mid;
                lo = mid + 1u;
            } else {
                hi = mid - 1u;
            }
        }
        return curve == DDM_OUTPUT_CURVE_GAMMA_0_5 ? (uint16_t)sqrt_x : (uint16_t)((sqrt_x + x + 1u) / 2u);
    }
    case DDM_OUTPUT_CURVE_GAMMA_1_5:
        return (uint16_t)((x + x2 + 1u) / 2u);
    case DDM_OUTPUT_CURVE_GAMMA_2_0:
        return (uint16_t)x2;
    case DDM_OUTPUT_CURVE_GAMMA_2_5:
        return (uint16_t)((x2 + x3 + 1u) / 2u);
    case DDM_OUTPUT_CURVE_GAMMA_3_0:
        return (uint16_t)x3;
    default:
        return (uint16_t)x;
    }
}

static uint16_t level_to_phase_level(unsigned int channel, uint16_t level) {
    uint16_t curved = curve_level(level, g_output_curve[channel]);
    uint16_t min = g_zone_min[channel];
    uint16_t max = g_zone_max[channel];
    if (max <= min) {
        return max;
    }
    return (uint16_t)(min + ((uint32_t)(max - min) * curved + DIMMING_MAX_LEVEL / 2u) / DIMMING_MAX_LEVEL);
}

static uint16_t level_to_compare(unsigned int channel, uint16_t level) {
    if (level == 0u) {
        return DIMMING_TIMER_STEPS + 1u;
    }
    level = level > DIMMING_MAX_LEVEL ? DIMMING_MAX_LEVEL : level;
    return (uint16_t)(DIMMING_TIMER_STEPS - level_to_phase_level(channel, level));
}

static uint32_t timer_clock_hz(void) {
    uint32_t timer_hz = rcc_apb1_frequency;
    uint32_t ppre1 = (RCC_CFGR >> RCC_CFGR_PPRE1_SHIFT) & RCC_CFGR_PPRE1_MASK;
    if (ppre1 >= RCC_CFGR_PPRE_DIV2) {
        timer_hz *= 2u;
    }
    return timer_hz;
}

static uint16_t prescaler_for_span(uint16_t span_us) {
    uint32_t timer_hz = timer_clock_hz();
    uint32_t divisor = ((timer_hz / 1000000u) * (uint32_t)span_us + 512u) / 1024u;
    if (divisor == 0u) {
        divisor = 1u;
    }
    if (divisor > 65536u) {
        divisor = 65536u;
    }
    return (uint16_t)(divisor - 1u);
}

static void update_measured_period(uint32_t cnt) {
    if (!g_have_zc_sample) {
        g_have_zc_sample = true;
        return;
    }

    uint32_t timer_ticks_per_us = timer_clock_hz() / 1000000u;
    uint32_t elapsed_ticks = (cnt + 1u) * (TIM_PSC(TIM3) + 1u);
    uint32_t sample_us = (elapsed_ticks + timer_ticks_per_us / 2u) / timer_ticks_per_us;
    if (sample_us < TRIAC_VALID_PERIOD_MIN_US || sample_us > TRIAC_VALID_PERIOD_MAX_US) {
        return;
    }

    if (g_measured_half_period_us == 0u) {
        g_measured_half_period_us = (uint16_t)sample_us;
    } else {
        g_measured_half_period_us =
                (uint16_t)(((uint32_t)g_measured_half_period_us * 7u + sample_us + 4u) / 8u);
    }

    uint16_t desired_span = (uint16_t)(g_measured_half_period_us + TRIAC_ZC_GUARD_US);
    uint16_t delta = desired_span > g_timer_span_us ? (uint16_t)(desired_span - g_timer_span_us)
                                                    : (uint16_t)(g_timer_span_us - desired_span);
    if (delta >= TRIAC_RETUNE_THRESHOLD_US) {
        timer_set_prescaler(TIM3, prescaler_for_span(desired_span));
        g_timer_span_us = desired_span;
    }
}

static void apply_channel_level(unsigned int channel, uint16_t level) {
    g_shadow_level[channel] = level;
    g_applied_level[channel] = level;
}

static void advance_fades(uint32_t now_ms) {
    for (unsigned int i = 0u; i < 4u; i++) {
        if (!g_fade_active[i]) {
            continue;
        }

        uint16_t target = g_target_level[i];
        uint16_t from = g_fade_from_level[i];
        uint16_t duration = g_fade_duration_ms[i];
        uint32_t elapsed = now_ms - g_fade_started_ms[i];

        if (duration == 0u || elapsed >= duration) {
            g_fade_active[i] = false;
            apply_channel_level(i, target);
            continue;
        }

        int32_t delta = (int32_t)target - (int32_t)from;
        uint16_t level = (uint16_t)((int32_t)from + (delta * (int32_t)elapsed) / (int32_t)duration);
        apply_channel_level(i, level);
    }
}

void dimming_setup(void) {
    rcc_periph_clock_enable(RCC_TIM3);
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);

    /* PA5 startup pulse */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_AUX_CONTROL);
    gpio_set(GPIOA, GPIO_AUX_CONTROL);
    timebase_delay_ms(100);
    gpio_clear(GPIOA, GPIO_AUX_CONTROL);

    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO6 | GPIO7);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO0 | GPIO1);

    timer_disable_counter(TIM3);
    timer_set_prescaler(TIM3, prescaler_for_span(TRIAC_INITIAL_TIMER_SPAN_US));
    timer_set_period(TIM3, DIMMING_TIMER_STEPS);
    timer_set_counter(TIM3, 0);

    timer_disable_oc_output(TIM3, TIM_OC1);
    timer_disable_oc_output(TIM3, TIM_OC2);
    timer_disable_oc_output(TIM3, TIM_OC3);
    timer_disable_oc_output(TIM3, TIM_OC4);

    timer_set_oc_mode(TIM3, TIM_OC1, TIM_OCM_PWM1);
    timer_set_oc_mode(TIM3, TIM_OC2, TIM_OCM_PWM1);
    timer_set_oc_mode(TIM3, TIM_OC3, TIM_OCM_PWM1);
    timer_set_oc_mode(TIM3, TIM_OC4, TIM_OCM_PWM1);
    timer_set_oc_polarity_low(TIM3, TIM_OC1);
    timer_set_oc_polarity_low(TIM3, TIM_OC2);
    timer_set_oc_polarity_low(TIM3, TIM_OC3);
    timer_set_oc_polarity_low(TIM3, TIM_OC4);
    timer_set_oc_value(TIM3, TIM_OC1, level_to_compare(0u, 0u));
    timer_set_oc_value(TIM3, TIM_OC2, level_to_compare(1u, 0u));
    timer_set_oc_value(TIM3, TIM_OC3, level_to_compare(2u, 0u));
    timer_set_oc_value(TIM3, TIM_OC4, level_to_compare(3u, 0u));

    timer_enable_oc_output(TIM3, TIM_OC1);
    timer_enable_oc_output(TIM3, TIM_OC2);
    timer_enable_oc_output(TIM3, TIM_OC3);
    timer_enable_oc_output(TIM3, TIM_OC4);

    /* PA0 EXTI0 falling edge zero-cross. */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO0);
    exti_select_source(EXTI0, GPIOA);
    exti_set_trigger(EXTI0, EXTI_TRIGGER_FALLING);
    exti_enable_request(EXTI0);
    nvic_enable_irq(NVIC_EXTI0_IRQ);

    g_measured_half_period_us = 0u;
    g_timer_span_us = TRIAC_INITIAL_TIMER_SPAN_US;
    g_have_zc_sample = false;
    for (unsigned int i = 0u; i < 4u; i++) {
        g_shadow_level[i] = 0u;
        g_target_level[i] = 0u;
        g_applied_level[i] = 0u;
        g_fade_from_level[i] = 0u;
        g_fade_started_ms[i] = 0u;
        g_fade_duration_ms[i] = 0u;
        g_fade_active[i] = false;
        g_zone_min[i] = 0u;
        g_zone_max[i] = DIMMING_MAX_LEVEL;
        g_output_curve[i] = DDM_OUTPUT_CURVE_LINEAR;
    }
    timer_enable_counter(TIM3);
}

void dimming_set_channel_profile(unsigned int channel, uint16_t min_level, uint16_t max_level, uint16_t output_curve) {
    if (channel >= 4u) {
        return;
    }
    if (min_level > DIMMING_MAX_LEVEL) {
        min_level = DIMMING_MAX_LEVEL;
    }
    if (max_level > DIMMING_MAX_LEVEL) {
        max_level = DIMMING_MAX_LEVEL;
    }
    if (min_level > max_level) {
        min_level = max_level;
    }
    if (output_curve > DDM_OUTPUT_CURVE_MAX) {
        output_curve = DDM_OUTPUT_CURVE_LINEAR;
    }
    g_zone_min[channel] = min_level;
    g_zone_max[channel] = max_level;
    g_output_curve[channel] = output_curve;
}

void dimming_set_channel_level(unsigned int channel, uint16_t level, uint16_t fade_ms) {
    if (channel >= 4u) {
        return;
    }
    if (level > DIMMING_MAX_LEVEL) {
        level = DIMMING_MAX_LEVEL;
    }
    uint32_t now_ms = timebase_ms();
    advance_fades(now_ms);

    g_target_level[channel] = level;
    if (fade_ms == 0u || g_applied_level[channel] == level) {
        g_fade_active[channel] = false;
        g_fade_duration_ms[channel] = 0u;
        g_fade_from_level[channel] = level;
        apply_channel_level(channel, level);
        return;
    }

    g_fade_from_level[channel] = g_applied_level[channel];
    g_fade_started_ms[channel] = now_ms;
    g_fade_duration_ms[channel] = fade_ms;
    g_fade_active[channel] = true;
}

void exti0_isr(void) {
    exti_reset_request(EXTI0);

    /*
	 * Ignore re-triggers in the first ~25% of the half-period (CNT in [1, 256]).
	 * Protects against spurious EXTI0 pulses from zero-cross circuit noise.
	 * CNT == 0 is allowed: it means the timer just reset, and a clean ZC arrived.
	 */
    uint32_t cnt = TIM_CNT(TIM3);
    if (cnt >= 1u && cnt <= 256u) {
        return;
    }

    update_measured_period(cnt);

    /* Apply pending level changes synchronously at zero-cross. */
    timer_set_oc_value(TIM3, TIM_OC4, level_to_compare(0u, g_shadow_level[0]));
    timer_set_oc_value(TIM3, TIM_OC3, level_to_compare(1u, g_shadow_level[1]));
    timer_set_oc_value(TIM3, TIM_OC2, level_to_compare(2u, g_shadow_level[2]));
    timer_set_oc_value(TIM3, TIM_OC1, level_to_compare(3u, g_shadow_level[3]));

    timer_set_counter(TIM3, DIMMING_TIMER_STEPS);
    timer_enable_counter(TIM3);
}

void dimming_poll(uint32_t now_ms) {
    advance_fades(now_ms);
    for (unsigned int i = 0u; i < 4u; i++) {
        if (!g_fade_active[i] && g_applied_level[i] != g_target_level[i]) {
            apply_channel_level(i, g_target_level[i]);
        }
    }
}
