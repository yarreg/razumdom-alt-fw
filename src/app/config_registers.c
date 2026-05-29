#include "config_registers.h"

#include "ddm_config.h"

static struct ddm_config g_reg_config;
static uint16_t g_status;
static enum ddm_config_register_command g_command;

static bool apply_candidate(const struct ddm_config *candidate) {
    uint16_t status = DDM_CONFIG_REG_OK;
    if (!ddm_config_validate(candidate, &status)) {
        g_status = status;
        return false;
    }
    g_reg_config = *candidate;
    buttons_set_config(&g_reg_config);
    g_status = DDM_CONFIG_REG_OK;
    return true;
}

static struct ddm_channel_profile *profile_for_hr(uint16_t hr, uint16_t *offset) {
    if (hr < DDM_HR_CHANNEL_PROFILE_BASE) {
        return 0;
    }
    uint16_t rel = (uint16_t)(hr - DDM_HR_CHANNEL_PROFILE_BASE);
    uint8_t index = (uint8_t)(rel / DDM_HR_CHANNEL_PROFILE_STRIDE);
    *offset = (uint16_t)(rel % DDM_HR_CHANNEL_PROFILE_STRIDE);
    if (index >= DDM_OUTPUT_COUNT) {
        return 0;
    }
    return &g_reg_config.channels[index];
}

static struct ddm_button_binding *binding_for_hr(uint16_t hr, uint16_t *offset) {
    if (hr < DDM_HR_CONFIG_BINDING_BASE) {
        return 0;
    }
    uint16_t rel = (uint16_t)(hr - DDM_HR_CONFIG_BINDING_BASE);
    uint8_t index = (uint8_t)(rel / DDM_HR_CONFIG_BINDING_STRIDE);
    *offset = (uint16_t)(rel % DDM_HR_CONFIG_BINDING_STRIDE);
    if (index >= DDM_BUTTON_BINDING_MAX) {
        return 0;
    }
    return &g_reg_config.bindings[index];
}

void config_registers_init(void) {
    ddm_config_set_defaults(&g_reg_config);
    g_status = DDM_CONFIG_REG_OK;
    g_command = DDM_CONFIG_CMD_NONE;
    buttons_init(&g_reg_config);
}

const struct ddm_config *config_registers_config(void) {
    return &g_reg_config;
}

bool config_registers_apply_config(const struct ddm_config *config) {
    return apply_candidate(config);
}

enum ddm_config_register_command config_registers_take_command(void) {
    enum ddm_config_register_command command = g_command;
    g_command = DDM_CONFIG_CMD_NONE;
    return command;
}

uint16_t config_registers_status(void) {
    return g_status;
}

bool config_registers_hr_read(uint16_t hr, uint16_t *value) {
    if (hr == DDM_HR_CONFIG_COMMAND) {
        *value = 0u;
    } else if (hr == DDM_HR_CONFIG_STATUS) {
        *value = g_status;
    } else if (hr == DDM_HR_CONFIG_BINDING_COUNT) {
        *value = g_reg_config.binding_count;
    } else if (hr == DDM_HR_CONFIG_DI_ADC_THRESHOLD) {
        *value = g_reg_config.di_adc_active_threshold;
    } else if (hr == DDM_HR_CONFIG_DEBOUNCE_MS) {
        *value = g_reg_config.debounce_ms;
    } else if (hr == DDM_HR_CONFIG_SHORT_MIN_MS) {
        *value = g_reg_config.short_press_min_ms;
    } else if (hr == DDM_HR_CONFIG_SHORT_MAX_MS) {
        *value = g_reg_config.short_press_max_ms;
    } else if (hr == DDM_HR_CONFIG_LONG_MS) {
        *value = g_reg_config.long_press_ms;
    } else if (hr == DDM_HR_CONFIG_AUTO_LIGHT_SUPPRESS_S) {
        *value = g_reg_config.auto_light_suppress_s;
    } else {
        uint16_t offset;
        struct ddm_channel_profile *profile = profile_for_hr(hr, &offset);
        if (profile != 0) {
            switch (offset) {
            case 0:
                *value = profile->zone.min_level;
                return true;
            case 1:
                *value = profile->zone.max_level;
                return true;
            case 2:
                *value = profile->default_level;
                return true;
            case 3:
                *value = profile->night_level;
                return true;
            case 4:
                *value = profile->fade_on_ms;
                return true;
            case 5:
                *value = profile->fade_off_ms;
                return true;
            case 6:
                *value = profile->output_curve;
                return true;
            default:
                *value = 0u;
                return true;
            }
        }

        struct ddm_button_binding *b = binding_for_hr(hr, &offset);
        if (b == 0 || offset >= DDM_HR_CONFIG_BINDING_STRIDE) {
            return false;
        }
        switch (offset) {
        case 0:
            *value = b->enabled ? 1u : 0u;
            break;
        case 1:
            *value = b->type;
            break;
        case 2:
            *value = b->target_type;
            break;
        case 3:
            *value = b->target_id;
            break;
        case 4:
            *value = b->di_primary;
            break;
        case 5:
            *value = (b->di_secondary == DDM_DI_NONE) ? 0xffffu : b->di_secondary;
            break;
        case 6:
            *value = b->hold_step;
            break;
        case 7:
            *value = b->hold_period_ms;
            break;
        case 8:
            *value = b->flags;
            break;
        default:
            *value = 0u;
            break;
        }
    }
    return true;
}

bool config_registers_hr_write(uint16_t hr, uint16_t value) {
    struct ddm_config candidate = g_reg_config;

    if (hr == DDM_HR_CONFIG_COMMAND) {
        if (value == 0u) {
            g_status = DDM_CONFIG_REG_OK;
            return true;
        }
        if (value == 1u || value == 2u) {
            g_command = (enum ddm_config_register_command)value;
        g_status = DDM_CONFIG_REG_OK;
        return true;
    }
    if (value == 3u) {
        ddm_config_set_defaults(&candidate);
        if (!apply_candidate(&candidate)) {
            return false;
        }
            g_command = DDM_CONFIG_CMD_FACTORY_DEFAULTS;
            return true;
        }
        g_status = DDM_CONFIG_REG_ERR_RANGE;
        return false;
    } else if (hr == DDM_HR_CONFIG_STATUS) {
        return false;
    } else if (hr == DDM_HR_CONFIG_BINDING_COUNT) {
        candidate.binding_count = (uint8_t)value;
    } else if (hr == DDM_HR_CONFIG_DI_ADC_THRESHOLD) {
        candidate.di_adc_active_threshold = value;
    } else if (hr == DDM_HR_CONFIG_DEBOUNCE_MS) {
        candidate.debounce_ms = value;
    } else if (hr == DDM_HR_CONFIG_SHORT_MIN_MS) {
        candidate.short_press_min_ms = value;
    } else if (hr == DDM_HR_CONFIG_SHORT_MAX_MS) {
        candidate.short_press_max_ms = value;
    } else if (hr == DDM_HR_CONFIG_LONG_MS) {
        candidate.long_press_ms = value;
    } else if (hr == DDM_HR_CONFIG_AUTO_LIGHT_SUPPRESS_S) {
        candidate.auto_light_suppress_s = value;
    } else {
        uint16_t offset;
        struct ddm_channel_profile *profile = profile_for_hr(hr, &offset);
        if (profile != 0) {
            uint8_t index = (uint8_t)((hr - DDM_HR_CHANNEL_PROFILE_BASE) / DDM_HR_CHANNEL_PROFILE_STRIDE);
            profile = &candidate.channels[index];
            switch (offset) {
            case 0:
                profile->zone.min_level = value;
                break;
            case 1:
                profile->zone.max_level = value;
                break;
            case 2:
                profile->default_level = value;
                break;
            case 3:
                profile->night_level = value;
                break;
            case 4:
                profile->fade_on_ms = value;
                break;
            case 5:
                profile->fade_off_ms = value;
                break;
            case 6:
                profile->output_curve = value;
                break;
            default:
                if (value != 0u) {
                    g_status = DDM_CONFIG_REG_ERR_RESERVED;
                    return false;
                }
                return true;
            }
            return apply_candidate(&candidate);
        }

        struct ddm_button_binding *b = binding_for_hr(hr, &offset);
        if (b == 0 || offset >= DDM_HR_CONFIG_BINDING_STRIDE) {
            return false;
        }
        uint8_t index = (uint8_t)((hr - DDM_HR_CONFIG_BINDING_BASE) / DDM_HR_CONFIG_BINDING_STRIDE);
        b = &candidate.bindings[index];
        switch (offset) {
        case 0:
            b->enabled = value != 0u;
            break;
        case 1:
            b->type = (uint8_t)value;
            break;
        case 2:
            b->target_type = (uint8_t)value;
            break;
        case 3:
            b->target_id = (uint8_t)value;
            break;
        case 4:
            b->di_primary = (uint8_t)value;
            break;
        case 5:
            b->di_secondary = (value == 0xffffu) ? DDM_DI_NONE : (uint8_t)value;
            break;
        case 6:
            b->hold_step = value;
            break;
        case 7:
            b->hold_period_ms = value;
            break;
        case 8:
            b->flags = value;
            break;
        default:
            if (value != 0u) {
                g_status = DDM_CONFIG_REG_ERR_RESERVED;
                return false;
            }
            return true;
        }
    }

    return apply_candidate(&candidate);
}
