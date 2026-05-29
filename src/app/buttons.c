#include "buttons.h"

#include "channels.h"

#include <string.h>

struct binding_runtime {
    bool primary_pressed;
    bool secondary_pressed;
    bool long_started_primary;
    bool long_started_secondary;
    uint32_t primary_pressed_ms;
    uint32_t secondary_pressed_ms;
};

static struct ddm_config g_config;
static struct binding_runtime g_runtime[DDM_BUTTON_BINDING_MAX];

static bool binding_targets_group(const struct ddm_button_binding *binding) {
    return binding->target_type == DDM_BINDING_TARGET_GROUP && binding->target_id == DDM_GROUP_ALL_CHANNELS;
}

static uint8_t binding_channel(const struct ddm_button_binding *binding) {
    return binding->target_id;
}

static bool binding_is_rocker(const struct ddm_button_binding *binding) {
    return binding->type == DDM_BUTTON_ROCKER_UP_DOWN;
}

static uint16_t binding_level(const struct ddm_button_binding *binding) {
    if (!binding_targets_group(binding)) {
        return channels_level(binding_channel(binding));
    }

    uint16_t level = 0u;
    for (uint8_t channel = 1u; channel <= DDM_OUTPUT_COUNT; channel++) {
        uint16_t channel_level = channels_level(channel);
        if (channel_level > level) {
            level = channel_level;
        }
    }
    return level;
}

static void start_channel(uint8_t channel, uint16_t target, bool use_profile_fade) {
    if (use_profile_fade) {
        channels_apply_level_with_profile_fade(channel, target);
    } else {
        channels_apply_level(channel, target, 0u);
    }
}

static void start_group_level(uint16_t target, bool use_profile_fade) {
    channels_apply_all_level(target, use_profile_fade);
}

static void start_group_startup(void) {
    for (uint8_t channel = 1u; channel <= DDM_OUTPUT_COUNT; channel++) {
        start_channel(channel, channels_startup_level(channel), true);
    }
}

static bool start_group_night(void) {
    bool applied = false;
    for (uint8_t channel = 1u; channel <= DDM_OUTPUT_COUNT; channel++) {
        uint16_t target = channels_night_level(channel);
        if (target == 0u) {
            continue;
        }
        channels_set_on_from_rocker_down_min(channel, true);
        start_channel(channel, target, true);
        applied = true;
    }
    return applied;
}

static void remember_target_nonzero(const struct ddm_button_binding *binding) {
    if (binding_targets_group(binding)) {
        for (uint8_t channel = 1u; channel <= DDM_OUTPUT_COUNT; channel++) {
            channels_remember_nonzero(channel);
        }
    } else {
        channels_remember_nonzero(binding_channel(binding));
    }
}

static void clear_group_rocker_down_min(void) {
    for (uint8_t channel = 1u; channel <= DDM_OUTPUT_COUNT; channel++) {
        channels_set_on_from_rocker_down_min(channel, false);
    }
}

static void rocker_release(uint8_t i, const struct ddm_button_binding *binding, bool is_up, uint32_t duration_ms) {
    bool is_group = binding_targets_group(binding);
    uint8_t channel = is_group ? 1u : binding_channel(binding);
    struct ddm_channel_runtime *state = channels_state_mut(channel);
    bool was_long = is_up ? g_runtime[i].long_started_primary : g_runtime[i].long_started_secondary;

    if (state == 0) {
        return;
    }
    if (was_long) {
        state->ramp_due_ms = 0u;
        remember_target_nonzero(binding);
        return;
    }
    if (duration_ms < g_config.short_press_min_ms || duration_ms > g_config.short_press_max_ms) {
        return;
    }

    if (is_up) {
        if (is_group) {
            clear_group_rocker_down_min();
            if (binding_level(binding) == 0u) {
                start_group_startup();
            } else {
                start_group_level(DDM_LEVEL_MAX, true);
            }
        } else {
            uint16_t target = state->level == 0u ? channels_startup_level(channel) : DDM_LEVEL_MAX;
            channels_set_on_from_rocker_down_min(channel, false);
            start_channel(channel, target, true);
        }
        return;
    }

    if (binding_level(binding) == 0u) {
        if (is_group) {
            (void)start_group_night();
            return;
        }
        uint16_t target = channels_night_level(channel);
        if (target == 0u) {
            return;
        }
        channels_set_on_from_rocker_down_min(channel, true);
        start_channel(channel, target, true);
        return;
    }

    if (is_group) {
        start_group_level(0u, true);
    } else {
        start_channel(channel, 0u, true);
    }
}

static void momentary_release(uint8_t i, const struct ddm_button_binding *binding, uint32_t duration_ms) {
    bool is_group = binding_targets_group(binding);
    uint8_t channel = is_group ? 1u : binding_channel(binding);
    struct ddm_channel_runtime *state = channels_state_mut(channel);

    if (state == 0) {
        return;
    }
    if (g_runtime[i].long_started_primary) {
        state->ramp_due_ms = 0u;
        remember_target_nonzero(binding);
        return;
    }
    if (duration_ms < g_config.short_press_min_ms || duration_ms > g_config.short_press_max_ms) {
        return;
    }

    if (binding_level(binding) == 0u) {
        if (is_group) {
            clear_group_rocker_down_min();
            start_group_startup();
        } else {
            channels_set_on_from_rocker_down_min(channel, false);
            start_channel(channel, channels_startup_level(channel), true);
        }
    } else if (is_group) {
        start_group_level(0u, true);
    } else {
        start_channel(channel, 0u, true);
    }
}

void buttons_init(const struct ddm_config *config) {
    memset(g_runtime, 0, sizeof(g_runtime));
    channels_init(config);
    buttons_set_config(config);
}

void buttons_set_config(const struct ddm_config *config) {
    g_config = *config;
    memset(g_runtime, 0, sizeof(g_runtime));
    channels_set_config(config);
}

const struct ddm_config *buttons_config(void) {
    return &g_config;
}

void buttons_on_input_event(const struct ddm_input_event *event, uint32_t now_ms) {
    if (event->type != DDM_INPUT_EVENT_DEBOUNCED_PRESS && event->type != DDM_INPUT_EVENT_DEBOUNCED_RELEASE) {
        return;
    }

    for (uint8_t i = 0; i < g_config.binding_count; i++) {
        const struct ddm_button_binding *b = &g_config.bindings[i];
        if (!b->enabled) {
            continue;
        }

        bool primary = event->di == b->di_primary;
        bool secondary = b->type == DDM_BUTTON_ROCKER_UP_DOWN && event->di == b->di_secondary;
        if (!primary && !secondary) {
            continue;
        }

        if (event->type == DDM_INPUT_EVENT_DEBOUNCED_PRESS) {
            if (primary) {
                g_runtime[i].primary_pressed = true;
                g_runtime[i].primary_pressed_ms = now_ms;
            } else {
                g_runtime[i].secondary_pressed = true;
                g_runtime[i].secondary_pressed_ms = now_ms;
            }
            if (b->type == DDM_BUTTON_LATCHING_SWITCH) {
                uint8_t channel = binding_channel(b);
                start_channel(channel, channels_startup_level(channel), true);
            }
            return;
        }

        if (primary) {
            g_runtime[i].primary_pressed = false;
        } else {
            g_runtime[i].secondary_pressed = false;
        }

        if (b->type == DDM_BUTTON_ROCKER_UP_DOWN) {
            rocker_release(i, b, primary, event->duration_ms);
            if (primary) {
                g_runtime[i].long_started_primary = false;
            } else {
                g_runtime[i].long_started_secondary = false;
            }
        } else if (b->type == DDM_BUTTON_MOMENTARY_PUSH) {
            momentary_release(i, b, event->duration_ms);
            g_runtime[i].long_started_primary = false;
        } else if (b->type == DDM_BUTTON_LATCHING_SWITCH) {
            start_channel(binding_channel(b), 0u, true);
        }
        return;
    }
}

void buttons_tick(uint32_t now_ms) {
    for (uint8_t i = 0; i < g_config.binding_count; i++) {
        const struct ddm_button_binding *b = &g_config.bindings[i];
        bool is_rocker = binding_is_rocker(b);
        bool is_momentary = b->type == DDM_BUTTON_MOMENTARY_PUSH;
        bool is_group = binding_targets_group(b);
        if (!b->enabled || (!is_rocker && !is_momentary)) {
            continue;
        }
        uint8_t channel = is_group ? 1u : binding_channel(b);
        struct ddm_channel_runtime *state = channels_state_mut(channel);
        if (state == 0) {
            continue;
        }

        bool primary_long =
                g_runtime[i].primary_pressed && now_ms - g_runtime[i].primary_pressed_ms >= g_config.long_press_ms;
        bool secondary_long =
                g_runtime[i].secondary_pressed && now_ms - g_runtime[i].secondary_pressed_ms >= g_config.long_press_ms;

        if (is_rocker && g_runtime[i].primary_pressed && g_runtime[i].secondary_pressed) {
            continue;
        }

        if (primary_long && !g_runtime[i].long_started_primary) {
            g_runtime[i].long_started_primary = true;
            state->ramp_due_ms = now_ms;
        }
        if (secondary_long && !g_runtime[i].long_started_secondary && binding_level(b) != 0u) {
            g_runtime[i].long_started_secondary = true;
            state->ramp_due_ms = now_ms;
        }

        int8_t dir = 0;
        if (g_runtime[i].long_started_primary) {
            dir = is_momentary ? state->momentary_ramp_dir : 1;
        } else if (g_runtime[i].long_started_secondary) {
            dir = -1;
        }
        if (dir == 0 || now_ms < state->ramp_due_ms) {
            continue;
        }

        uint16_t next;
        uint16_t current_level = binding_level(b);
        if (dir > 0) {
            next = (uint16_t)(current_level + b->hold_step);
            if (next >= DDM_LEVEL_MAX || next < current_level) {
                next = DDM_LEVEL_MAX;
                if (is_momentary) {
                    state->momentary_ramp_dir = -1;
                }
            }
        } else {
            next = current_level > b->hold_step ? (uint16_t)(current_level - b->hold_step) : 0u;
            if (next <= 1u) {
                next = 1u;
                if (is_momentary) {
                    state->momentary_ramp_dir = 1;
                }
            }
        }

        if (is_group) {
            start_group_level(next, false);
        } else {
            start_channel(channel, next, false);
        }
        state->ramp_due_ms = now_ms + b->hold_period_ms;
    }
}
