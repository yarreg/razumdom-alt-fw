#include "dimming.h"

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "ddm_config.h"
#include "timebase.h"

#define DIMMING_MAX_LEVEL   1023u
#define DIMMING_TIMER_STEPS 1022u

static uint16_t g_target_level[4];
static uint16_t g_applied_level[4];
static uint16_t g_fade_from_level[4];
static uint16_t g_zone_min[4];
static uint16_t g_zone_max[4];
static uint16_t g_output_curve[4];
static uint32_t g_fade_started_ms[4];
static uint16_t g_fade_duration_ms[4];
static bool g_fade_active[4];

/*
 * DDL84R LED drivers are active-low. CH1/CH2 use a leading PWM pulse,
 * while CH3/CH4 use an equivalent trailing pulse. Splitting equal RGBW
 * loads across the two phases avoids four simultaneous supply-current
 * transitions at partial brightness.
 */
static uint16_t leading_level_to_compare(uint16_t level) {
    if (level >= DIMMING_MAX_LEVEL) {
        return DIMMING_TIMER_STEPS + 1u;
    }
    return level;
}

static uint16_t trailing_level_to_compare(uint16_t level) {
    if (level >= DIMMING_MAX_LEVEL) {
        return 0u;
    }
    return (uint16_t)(DIMMING_TIMER_STEPS + 1u - level);
}

static uint16_t pwm_prescaler(void) {
    /* Stock DDL84R uses ARR=1022 and PSC=120, approximately 97 Hz at 12 MHz. */
    return 120u;
}

static uint16_t level_to_output_level(unsigned int channel, uint16_t level);

static void apply_channel_level(unsigned int channel, uint16_t level) {
    uint16_t logical_level = level;
    level = level_to_output_level(channel, level);
    /* DDL84R TIM3 channel mapping:
	 * Logical 0 -> TIM3_OC1/PA6, 1 -> OC2/PA7, 2 -> OC3/PB0, 3 -> OC4/PB1. */
    switch (channel) {
    case 0:
        timer_set_oc_value(TIM3, TIM_OC1, leading_level_to_compare(level));
        break;
    case 1:
        timer_set_oc_value(TIM3, TIM_OC2, leading_level_to_compare(level));
        break;
    case 2:
        timer_set_oc_value(TIM3, TIM_OC3, trailing_level_to_compare(level));
        break;
    case 3:
        timer_set_oc_value(TIM3, TIM_OC4, trailing_level_to_compare(level));
        break;
    }
    g_applied_level[channel] = logical_level;
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
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);

    /* PWM output pins: PA6(CH1), PA7(CH2), PB0(CH3), PB1(CH4). */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO6 | GPIO7);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO0 | GPIO1);

    timer_disable_counter(TIM3);
    timer_set_prescaler(TIM3, pwm_prescaler());
    timer_set_period(TIM3, DIMMING_TIMER_STEPS);
    timer_set_counter(TIM3, 0);

    timer_disable_oc_output(TIM3, TIM_OC1);
    timer_disable_oc_output(TIM3, TIM_OC2);
    timer_disable_oc_output(TIM3, TIM_OC3);
    timer_disable_oc_output(TIM3, TIM_OC4);

    /* Active-low output: two channels lead and two trail at equal duty. */
    timer_set_oc_mode(TIM3, TIM_OC1, TIM_OCM_PWM1);
    timer_set_oc_mode(TIM3, TIM_OC2, TIM_OCM_PWM1);
    timer_set_oc_mode(TIM3, TIM_OC3, TIM_OCM_PWM2);
    timer_set_oc_mode(TIM3, TIM_OC4, TIM_OCM_PWM2);
    timer_set_oc_polarity_low(TIM3, TIM_OC1);
    timer_set_oc_polarity_low(TIM3, TIM_OC2);
    timer_set_oc_polarity_low(TIM3, TIM_OC3);
    timer_set_oc_polarity_low(TIM3, TIM_OC4);
    timer_set_oc_value(TIM3, TIM_OC1, leading_level_to_compare(0u));
    timer_set_oc_value(TIM3, TIM_OC2, leading_level_to_compare(0u));
    timer_set_oc_value(TIM3, TIM_OC3, trailing_level_to_compare(0u));
    timer_set_oc_value(TIM3, TIM_OC4, trailing_level_to_compare(0u));

    timer_enable_oc_output(TIM3, TIM_OC1);
    timer_enable_oc_output(TIM3, TIM_OC2);
    timer_enable_oc_output(TIM3, TIM_OC3);
    timer_enable_oc_output(TIM3, TIM_OC4);

    timer_continuous_mode(TIM3);
    timer_enable_counter(TIM3);

    for (unsigned int i = 0u; i < 4u; i++) {
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

void dimming_poll(uint32_t now_ms) {
    advance_fades(now_ms);
    for (unsigned int i = 0u; i < 4u; i++) {
        if (!g_fade_active[i] && g_applied_level[i] != g_target_level[i]) {
            apply_channel_level(i, g_target_level[i]);
        }
    }
}
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

static uint16_t level_to_output_level(unsigned int channel, uint16_t level) {
    uint16_t curved = curve_level(level, g_output_curve[channel]);
    uint16_t min = g_zone_min[channel];
    uint16_t max = g_zone_max[channel];
    if (level == 0u || max == 0u) {
        return 0u;
    }
    if (max <= min) {
        return max;
    }
    return (uint16_t)(min + ((uint32_t)(max - min) * curved + DIMMING_MAX_LEVEL / 2u) / DIMMING_MAX_LEVEL);
}
