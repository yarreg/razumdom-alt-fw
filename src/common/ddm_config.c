#include "ddm_config.h"

#include <string.h>

static const struct ddm_button_binding factory_bindings[] = {
        {
                .enabled = 1,
                .type = DDM_BUTTON_ROCKER_UP_DOWN,
                .target_type = DDM_BINDING_TARGET_CHANNEL,
                .target_id = 1,
                .di_primary = 1,
                .di_secondary = 2,
                .hold_step = 8,
                .hold_period_ms = 20,
                .flags = 0,
        },
        {
                .enabled = 1,
                .type = DDM_BUTTON_MOMENTARY_PUSH,
                .target_type = DDM_BINDING_TARGET_CHANNEL,
                .target_id = 2,
                .di_primary = 3,
                .di_secondary = 0xffu,
                .hold_step = 8,
                .hold_period_ms = 20,
                .flags = 0,
        },
        {
                .enabled = 1,
                .type = DDM_BUTTON_LATCHING_SWITCH,
                .target_type = DDM_BINDING_TARGET_CHANNEL,
                .target_id = 3,
                .di_primary = 4,
                .di_secondary = 0xffu,
                .hold_step = 8,
                .hold_period_ms = 20,
                .flags = 0,
        },
};

void ddm_config_set_defaults(struct ddm_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->modbus_address = DDM_MODBUS_DEFAULT_ADDRESS;
    cfg->modbus_port_cfg = DDM_MODBUS_DEFAULT_PORT_CFG;
    cfg->binding_count = (uint16_t)(sizeof(factory_bindings) / sizeof(factory_bindings[0]));
    cfg->di_adc_active_threshold = DDM_DEFAULT_DI_ADC_THRESHOLD;
    cfg->debounce_ms = DDM_DEFAULT_DEBOUNCE_MS;
    cfg->short_press_min_ms = DDM_DEFAULT_SHORT_MIN_MS;
    cfg->short_press_max_ms = DDM_DEFAULT_SHORT_MAX_MS;
    cfg->long_press_ms = DDM_DEFAULT_LONG_MS;
    cfg->auto_light_suppress_s = DDM_DEFAULT_AUTO_LIGHT_SUPPRESS_S;
    for (uint8_t i = 0; i < DDM_CHANNEL_COUNT; i++) {
        cfg->channels[i].zone.min_level = 0u;
        cfg->channels[i].zone.max_level = 1023u;
        cfg->channels[i].default_level = 1023u;
        cfg->channels[i].night_level = 0u;
        cfg->channels[i].fade_on_ms = 0u;
        cfg->channels[i].fade_off_ms = 0u;
        cfg->channels[i].output_curve = DDM_OUTPUT_CURVE_LINEAR;
    }
    cfg->channels[0].zone.min_level = 80u;
    cfg->channels[0].zone.max_level = 900u;
    cfg->channels[0].default_level = 700u;
    cfg->channels[0].night_level = 80u;
    cfg->channels[0].fade_on_ms = 500u;
    cfg->channels[0].fade_off_ms = 500u;
    cfg->channels[1].zone.min_level = 60u;
    cfg->channels[1].zone.max_level = 850u;
    cfg->channels[1].default_level = 700u;
    cfg->channels[1].night_level = 60u;
    cfg->channels[1].fade_on_ms = 300u;
    cfg->channels[1].fade_off_ms = 300u;
    cfg->channels[2].zone.min_level = 100u;
    cfg->channels[2].night_level = 100u;
    memcpy(cfg->bindings, factory_bindings, sizeof(factory_bindings));
}

void ddm_runtime_set_defaults(struct ddm_runtime_state *rt) {
    memset(rt, 0, sizeof(*rt));
    rt->config_status = DDM_CONFIG_STATUS_OK;

    for (uint8_t i = 0; i < DDM_CHANNEL_COUNT; i++) {
        rt->channels[i].on = 0;
        rt->channels[i].brightness_percent_setpoint = 100;
        rt->channels[i].brightness_level = 1023;
        rt->channels[i].last_nonzero_level = 0;
    }
}

uint16_t ddm_percent_to_level(uint16_t percent) {
    if (percent == 0) {
        return 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint16_t level = (uint16_t)((percent * 1023u + 50u) / 100u);
    return level == 0 ? 1 : level;
}

uint16_t ddm_level_to_percent(uint16_t level) {
    if (level == 0) {
        return 0;
    }
    if (level > 1023) {
        level = 1023;
    }
    uint16_t percent = (uint16_t)((level * 100u + 511u) / 1023u);
    if (percent == 0) {
        percent = 1;
    }
    if (percent > 100) {
        percent = 100;
    }
    return percent;
}

static bool validate_binding(const struct ddm_button_binding *b) {
    if (!b->enabled) {
        return true;
    }
    if (b->type < DDM_BUTTON_ROCKER_UP_DOWN || b->type > DDM_BUTTON_MOMENTARY_PUSH) {
        return false;
    }
    bool is_rocker = (b->type == DDM_BUTTON_ROCKER_UP_DOWN);
    if (b->target_type == DDM_BINDING_TARGET_CHANNEL) {
        if (b->target_id < 1 || b->target_id > DDM_CHANNEL_COUNT) {
            return false;
        }
    } else if (b->target_type == DDM_BINDING_TARGET_GROUP) {
        if (b->target_id != DDM_GROUP_ALL_CHANNELS) {
            return false;
        }
        if (b->type == DDM_BUTTON_LATCHING_SWITCH) {
            return false;
        }
    } else {
        return false;
    }
    if (b->di_primary >= DDM_DI_COUNT) {
        return false;
    }
    if (is_rocker) {
        if (b->di_secondary >= DDM_DI_COUNT || b->di_secondary == b->di_primary) {
            return false;
        }
    } else if (b->di_secondary != 0xffu) {
        return false;
    }
    if (b->hold_step == 0 || b->hold_step > 1023) {
        return false;
    }
    if (b->hold_period_ms < 10 || b->hold_period_ms > 1000) {
        return false;
    }
    return true;
}

static bool validate_channel(const struct ddm_channel_profile *ch) {
    if (ch->zone.max_level > 1023 || ch->zone.min_level > ch->zone.max_level) {
        return false;
    }
    if (ch->default_level > 1023 || ch->night_level > 1023) {
        return false;
    }
    if (ch->fade_on_ms > 5000 || ch->fade_off_ms > 5000 || ch->output_curve > DDM_OUTPUT_CURVE_MAX) {
        return false;
    }
    return true;
}

bool ddm_config_validate(const struct ddm_config *cfg, uint16_t *status) {
    uint16_t used_di_mask = 0;

    if (cfg->modbus_address < 1 || cfg->modbus_address > 247) {
        goto invalid;
    }
    if (!ddm_modbus_port_cfg_valid(cfg->modbus_port_cfg)) {
        goto invalid;
    }
    if (cfg->binding_count > DDM_MAX_BUTTON_BINDINGS) {
        goto invalid;
    }
    if (cfg->di_adc_active_threshold > 4095) {
        goto invalid;
    }
    if (cfg->debounce_ms > 1000 || cfg->short_press_min_ms > cfg->short_press_max_ms) {
        goto invalid;
    }
    if (cfg->long_press_ms < cfg->short_press_max_ms || cfg->long_press_ms > 10000) {
        goto invalid;
    }
    if (cfg->auto_light_suppress_s > DDM_MAX_AUTO_LIGHT_SUPPRESS_S) {
        goto invalid;
    }
    for (uint8_t i = 0; i < DDM_CHANNEL_COUNT; i++) {
        if (!validate_channel(&cfg->channels[i])) {
            goto invalid;
        }
    }

    for (uint8_t i = 0; i < cfg->binding_count; i++) {
        const struct ddm_button_binding *b = &cfg->bindings[i];
        if (!validate_binding(b)) {
            goto invalid;
        }
        if (!b->enabled) {
            continue;
        }
        bool is_rocker = (b->type == DDM_BUTTON_ROCKER_UP_DOWN);
        uint16_t primary_bit = (uint16_t)(1u << b->di_primary);
        if (used_di_mask & primary_bit) {
            goto invalid;
        }
        used_di_mask |= primary_bit;
        if (is_rocker) {
            uint16_t secondary_bit = (uint16_t)(1u << b->di_secondary);
            if (used_di_mask & secondary_bit) {
                goto invalid;
            }
            used_di_mask |= secondary_bit;
        }
    }

    if (status) {
        *status = DDM_CONFIG_STATUS_OK;
    }
    return true;

invalid:
    if (status) {
        *status = DDM_CONFIG_STATUS_INVALID_VALUE;
    }
    return false;
}
