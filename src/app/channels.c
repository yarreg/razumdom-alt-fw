#include "channels.h"

#include "dimming.h"
#include "timebase.h"

#include <string.h>

static struct ddm_channel_profile g_profiles[DDM_CHANNEL_COUNT];
static struct ddm_channel_runtime g_channels[DDM_CHANNEL_COUNT];
static uint32_t g_auto_light_suppress_ms;

static bool valid_channel(uint8_t channel) {
    return channel >= 1u && channel <= DDM_CHANNEL_COUNT;
}

static uint8_t index_from_channel(uint8_t channel) {
    return (uint8_t)(channel - 1u);
}

uint16_t channels_percent_to_level(uint16_t percent) {
    if (percent > 100u) {
        percent = 100u;
    }
    if (percent == 0u) {
        return 0u;
    }
    uint32_t level = (percent * DDM_LEVEL_MAX + 50u) / 100u;
    return level == 0u ? 1u : (uint16_t)level;
}

uint8_t channels_level_to_percent(uint16_t level) {
    if (level == 0u) {
        return 0u;
    }
    if (level > DDM_LEVEL_MAX) {
        level = DDM_LEVEL_MAX;
    }
    uint32_t percent = (level * 100u + DDM_LEVEL_MAX / 2u) / DDM_LEVEL_MAX;
    if (percent == 0u) {
        percent = 1u;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    return (uint8_t)percent;
}

void channels_init(const struct ddm_config *config) {
    memset(g_channels, 0, sizeof(g_channels));
    for (uint8_t i = 0u; i < DDM_CHANNEL_COUNT; i++) {
        g_channels[i].percent_setpoint = 100u;
        g_channels[i].momentary_ramp_dir = 1;
    }
    channels_set_config(config);
}

void channels_set_config(const struct ddm_config *config) {
    if (config == 0) {
        return;
    }
    memcpy(g_profiles, config->channels, sizeof(g_profiles));
    g_auto_light_suppress_ms = (uint32_t)config->auto_light_suppress_s * 1000u;
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        const struct ddm_channel_profile *profile = channels_profile(channel);
        dimming_set_channel_profile(index_from_channel(channel), profile->zone.min_level, profile->zone.max_level,
                                    profile->output_curve);
        struct ddm_channel_runtime *state = channels_state_mut(channel);
        if (state != 0 && state->level != 0u) {
            uint16_t level = channels_clamp_level(channel, state->level);
            if (level != state->level) {
                state->level = level;
                dimming_set_channel_level(index_from_channel(channel), level, 0u);
            }
            state->percent_setpoint = channels_level_to_percent_for_channel(channel, level);
        }
    }
}

const struct ddm_channel_profile *channels_profile(uint8_t channel) {
    static const struct ddm_channel_profile fallback = {
            .zone = {.min_level = 0u, .max_level = DDM_LEVEL_MAX},
            .default_level = DDM_LEVEL_MAX,
            .night_level = 0u,
            .fade_on_ms = 0u,
            .fade_off_ms = 0u,
            .output_curve = DDM_OUTPUT_CURVE_LINEAR,
    };
    if (!valid_channel(channel)) {
        return &fallback;
    }
    return &g_profiles[index_from_channel(channel)];
}

const struct ddm_channel_runtime *channels_state(uint8_t channel) {
    if (!valid_channel(channel)) {
        return 0;
    }
    return &g_channels[index_from_channel(channel)];
}

struct ddm_channel_runtime *channels_state_mut(uint8_t channel) {
    if (!valid_channel(channel)) {
        return 0;
    }
    return &g_channels[index_from_channel(channel)];
}

uint16_t channels_level(uint8_t channel) {
    const struct ddm_channel_runtime *state = channels_state(channel);
    return state == 0 ? 0u : state->level;
}

uint8_t channels_percent(uint8_t channel) {
    const struct ddm_channel_runtime *state = channels_state(channel);
    return state == 0 || state->level == 0u ? 0u : state->percent_setpoint;
}

bool channels_on(uint8_t channel) {
    return channels_level(channel) != 0u;
}

bool channels_auto_active(uint8_t channel) {
    const struct ddm_channel_runtime *state = channels_state(channel);
    return state != 0 && state->auto_active;
}

static bool auto_suppressed(const struct ddm_channel_runtime *state, uint32_t now_ms) {
    return state->auto_suppressed_until_ms != 0u && (int32_t)(state->auto_suppressed_until_ms - now_ms) > 0;
}

uint16_t channels_auto_level(uint8_t channel) {
    const struct ddm_channel_runtime *state = channels_state(channel);
    return state != 0 && state->auto_active ? state->level : 0u;
}

uint8_t channels_auto_percent(uint8_t channel) {
    const struct ddm_channel_runtime *state = channels_state(channel);
    return state != 0 && state->auto_active ? state->percent_setpoint : 0u;
}

uint16_t channels_clamp_level(uint8_t channel, uint16_t level) {
    (void)channel;
    if (level > DDM_LEVEL_MAX) {
        return DDM_LEVEL_MAX;
    }
    return level;
}

uint16_t channels_startup_level(uint8_t channel) {
    const struct ddm_channel_profile *profile = channels_profile(channel);
    const struct ddm_channel_runtime *state = channels_state(channel);
    if (state != 0 && state->has_last_nonzero) {
        return channels_clamp_level(channel, state->last_nonzero_level);
    }
    if (profile->default_level != 0u) {
        return channels_clamp_level(channel, profile->default_level);
    }
    return DDM_LEVEL_MAX;
}

uint16_t channels_night_level(uint8_t channel) {
    uint16_t level = channels_profile(channel)->night_level;
    if (level == 0u) {
        return 0u;
    }
    return channels_clamp_level(channel, level);
}

uint16_t channels_transition_fade_ms(uint8_t channel, uint16_t from_level, uint16_t to_level) {
    if (!valid_channel(channel) || from_level == to_level) {
        return 0u;
    }
    const struct ddm_channel_profile *profile = channels_profile(channel);
    return to_level > from_level ? profile->fade_on_ms : profile->fade_off_ms;
}

void channels_remember_nonzero(uint8_t channel) {
    struct ddm_channel_runtime *state = channels_state_mut(channel);
    if (state != 0 && state->level != 0u && !state->on_from_rocker_down_min) {
        state->last_nonzero_level = state->level;
        state->has_last_nonzero = true;
    }
}

void channels_set_on_from_rocker_down_min(uint8_t channel, bool value) {
    struct ddm_channel_runtime *state = channels_state_mut(channel);
    if (state != 0) {
        state->on_from_rocker_down_min = value;
    }
}

void channels_apply_level(uint8_t channel, uint16_t level, uint16_t fade_ms) {
    struct ddm_channel_runtime *state = channels_state_mut(channel);
    if (state == 0) {
        return;
    }
    bool was_auto = state->auto_active;
    bool was_on = state->level != 0u;
    state->auto_active = false;
    if (level > DDM_LEVEL_MAX) {
        level = DDM_LEVEL_MAX;
    }
    if (level != 0u) {
        level = channels_clamp_level(channel, level);
    }
    if (level == 0u) {
        if (!was_auto) {
            channels_remember_nonzero(channel);
        }
        if (was_on && g_auto_light_suppress_ms != 0u) {
            state->auto_suppressed_until_ms = timebase_ms() + g_auto_light_suppress_ms;
        }
        state->on_from_rocker_down_min = false;
        state->percent_setpoint = 0u;
    } else {
        state->percent_setpoint = channels_level_to_percent_for_channel(channel, level);
    }
    state->level = level;
    dimming_set_channel_level(index_from_channel(channel), level, fade_ms);
}

void channels_apply_level_with_profile_fade(uint8_t channel, uint16_t level) {
    uint16_t current = channels_level(channel);
    if (level != 0u) {
        level = channels_clamp_level(channel, level);
    }
    channels_apply_level(channel, level, channels_transition_fade_ms(channel, current, level));
}

void channels_apply_all_level(uint16_t level, bool use_profile_fade) {
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        if (use_profile_fade) {
            channels_apply_level_with_profile_fade(channel, level);
        } else {
            channels_apply_level(channel, level, 0u);
        }
    }
}

void channels_turn_all_off(void) {
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        channels_apply_level(channel, 0u, 0u);
    }
}

bool channels_apply_auto_level_with_profile_fade(uint8_t channel, uint16_t level) {
    struct ddm_channel_runtime *state = channels_state_mut(channel);
    if (state == 0) {
        return false;
    }
    if (level > DDM_LEVEL_MAX) {
        level = DDM_LEVEL_MAX;
    }
    if (level != 0u) {
        level = channels_clamp_level(channel, level);
    }

    if (level == 0u) {
        state->auto_suppressed_until_ms = 0u;
        if (!state->auto_active) {
            return true;
        }
        uint16_t fade_ms = channels_transition_fade_ms(channel, state->level, 0u);
        state->auto_active = false;
        state->on_from_rocker_down_min = false;
        state->percent_setpoint = 0u;
        state->level = 0u;
        dimming_set_channel_level(index_from_channel(channel), 0u, fade_ms);
        return true;
    }

    if (state->level != 0u && !state->auto_active) {
        return true;
    }
    if (!state->auto_active && auto_suppressed(state, timebase_ms())) {
        return true;
    }

    uint16_t fade_ms = channels_transition_fade_ms(channel, state->level, level);
    state->auto_active = true;
    state->on_from_rocker_down_min = false;
    state->percent_setpoint = channels_level_to_percent_for_channel(channel, level);
    state->level = level;
    dimming_set_channel_level(index_from_channel(channel), level, fade_ms);
    return true;
}

uint16_t channels_percent_to_level_for_channel(uint8_t channel, uint16_t percent) {
    (void)channel;
    return channels_percent_to_level(percent);
}

uint8_t channels_level_to_percent_for_channel(uint8_t channel, uint16_t level) {
    (void)channel;
    return channels_level_to_percent(level);
}
