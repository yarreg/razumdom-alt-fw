#include "registers.h"

#include "config_registers.h"
#include "channels.h"
#include "ddm_config.h"
#include "modbus_registers.h"

#include <stddef.h>

void registers_defaults(struct app_registers *regs) {
    config_registers_init();
    regs->slave_address = DDM_MODBUS_DEFAULT_ADDRESS;
    regs->hr1_port_config = DDM_MODBUS_DEFAULT_PORT_CFG;
    regs->bootloader_requested = false;
}

typedef bool (*hr_read_fn)(const struct app_registers *, uint16_t, uint16_t *);
typedef bool (*hr_write_fn)(struct app_registers *, uint16_t, uint16_t);

static bool hr_slave_addr_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)hr;
    *v = r->slave_address;
    return true;
}

static bool hr_slave_addr_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)hr;
    if (!v || v > 247u) {
        return false;
    }
    r->slave_address = (uint8_t)v;
    return true;
}

static bool hr_port_cfg_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)hr;
    *v = r->hr1_port_config;
    return true;
}

static bool hr_port_cfg_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)hr;
    if (!ddm_modbus_port_cfg_valid(v)) {
        return false;
    }
    r->hr1_port_config = v;
    return true;
}

static bool hr_ch_level_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    *v = channels_level((uint8_t)(hr - HR_CH_LEVEL_BASE + 1u));
    return true;
}

static bool hr_ch_level_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    if (v > DDM_LEVEL_MAX) {
        return false;
    }
    channels_apply_level_with_profile_fade((uint8_t)(hr - HR_CH_LEVEL_BASE + 1u), v);
    return true;
}

static bool hr_ch_pct_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    *v = channels_percent((uint8_t)(hr - HR_CH_PERCENT_BASE + 1u));
    return true;
}

static bool hr_ch_pct_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    if (v > 100u) {
        return false;
    }
    uint8_t channel = (uint8_t)(hr - HR_CH_PERCENT_BASE + 1u);
    channels_apply_level_with_profile_fade(channel, channels_percent_to_level_for_channel(channel, v));
    struct ddm_channel_runtime *state = channels_state_mut(channel);
    if (state != 0 && v != 0u) {
        state->percent_setpoint = (uint8_t)v;
    }
    return true;
}

static bool hr_ch_on_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    *v = channels_on((uint8_t)(hr - HR_CH_ON_BASE + 1u)) ? 1u : 0u;
    return true;
}

static bool hr_ch_on_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    if (v > 1u) {
        return false;
    }
    uint8_t channel = (uint8_t)(hr - HR_CH_ON_BASE + 1u);
    if (v == 0u) {
        channels_apply_level_with_profile_fade(channel, 0u);
    } else if (channels_level(channel) == 0u) {
        channels_apply_level_with_profile_fade(channel, DDM_LEVEL_MAX);
        struct ddm_channel_runtime *state = channels_state_mut(channel);
        if (state != 0) {
            state->percent_setpoint = 100u;
        }
    } else {
        channels_apply_level_with_profile_fade(channel, channels_level(channel));
    }
    return true;
}

static bool hr_all_level_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    uint16_t max = 0u;
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        uint16_t level = channels_level(channel);
        if (level > max) {
            max = level;
        }
    }
    *v = max;
    return true;
}

static bool hr_all_level_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    (void)hr;
    if (v > DDM_LEVEL_MAX) {
        return false;
    }
    channels_apply_all_level(v, true);
    return true;
}

static bool hr_all_pct_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    uint16_t max = 0u;
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        uint16_t percent = channels_percent(channel);
        if (percent > max) {
            max = percent;
        }
    }
    *v = max;
    return true;
}

static bool hr_all_pct_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    (void)hr;
    if (v > 100u) {
        return false;
    }
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        channels_apply_level_with_profile_fade(channel, channels_percent_to_level_for_channel(channel, v));
        struct ddm_channel_runtime *state = channels_state_mut(channel);
        if (state != 0 && v != 0u) {
            state->percent_setpoint = (uint8_t)v;
        }
    }
    return true;
}

static bool hr_all_on_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    bool all_on = true;
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        if (!channels_on(channel)) {
            all_on = false;
            break;
        }
    }
    *v = all_on ? 1u : 0u;
    return true;
}

static bool hr_all_on_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    (void)hr;
    if (v > 1u) {
        return false;
    }
    if (v == 0u) {
        channels_apply_all_level(0u, true);
        return true;
    }
    for (uint8_t channel = 1u; channel <= DDM_CHANNEL_COUNT; channel++) {
        uint16_t level = channels_level(channel);
        channels_apply_level_with_profile_fade(channel, level == 0u ? DDM_LEVEL_MAX : level);
        struct ddm_channel_runtime *state = channels_state_mut(channel);
        if (state != 0 && level == 0u) {
            state->percent_setpoint = 100u;
        }
    }
    return true;
}

static bool hr_auto_level_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    *v = channels_auto_level((uint8_t)(hr - HR_AUTO_LEVEL_BASE + 1u));
    return true;
}

static bool hr_auto_level_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    if (v > DDM_LEVEL_MAX) {
        return false;
    }
    return channels_apply_auto_level_with_profile_fade((uint8_t)(hr - HR_AUTO_LEVEL_BASE + 1u), v);
}

static bool hr_auto_pct_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    *v = channels_auto_percent((uint8_t)(hr - HR_AUTO_PERCENT_BASE + 1u));
    return true;
}

static bool hr_auto_pct_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)r;
    if (v > 100u) {
        return false;
    }
    uint8_t channel = (uint8_t)(hr - HR_AUTO_PERCENT_BASE + 1u);
    return channels_apply_auto_level_with_profile_fade(channel, channels_percent_to_level_for_channel(channel, v));
}

static bool hr_fw_version_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    *v = FW_APP_VERSION;
    return true;
}

static bool hr_boot_version_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    *v = 1u;
    return true;
}

static bool hr_device_mode_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    *v = DEVICE_MODE_APPLICATION;
    return true;
}

static bool hr_build_hi_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    *v = (uint16_t)(FW_APP_BUILD_VERSION >> 16);
    return true;
}

static bool hr_build_lo_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    *v = (uint16_t)FW_APP_BUILD_VERSION;
    return true;
}

static bool hr_update_cmd_r(const struct app_registers *r, uint16_t hr, uint16_t *v) {
    (void)r;
    (void)hr;
    *v = UPDATE_CMD_IDLE;
    return true;
}

static bool hr_update_cmd_w(struct app_registers *r, uint16_t hr, uint16_t v) {
    (void)hr;
    if (v != UPDATE_ENTER_BOOTLOADER) {
        return false;
    }
    r->bootloader_requested = true;
    channels_turn_all_off();
    return true;
}

static const struct {
    uint16_t min;
    uint16_t max;
    hr_read_fn read;
    hr_write_fn write;
} hr_table[] = {
        {HR_SLAVE_ADDRESS, HR_SLAVE_ADDRESS, hr_slave_addr_r, hr_slave_addr_w},
        {HR_PORT_CONFIG, HR_PORT_CONFIG, hr_port_cfg_r, hr_port_cfg_w},
        {HR_CH_LEVEL_BASE, HR_CH_LEVEL_END, hr_ch_level_r, hr_ch_level_w},
        {HR_CH_PERCENT_BASE, HR_CH_PERCENT_END, hr_ch_pct_r, hr_ch_pct_w},
        {HR_CH_ON_BASE, HR_CH_ON_END, hr_ch_on_r, hr_ch_on_w},
        {HR_ALL_LEVEL, HR_ALL_LEVEL, hr_all_level_r, hr_all_level_w},
        {HR_ALL_PERCENT, HR_ALL_PERCENT, hr_all_pct_r, hr_all_pct_w},
        {HR_ALL_ON, HR_ALL_ON, hr_all_on_r, hr_all_on_w},
        {HR_AUTO_LEVEL_BASE, HR_AUTO_LEVEL_END, hr_auto_level_r, hr_auto_level_w},
        {HR_AUTO_PERCENT_BASE, HR_AUTO_PERCENT_END, hr_auto_pct_r, hr_auto_pct_w},
        {HR_FW_VERSION, HR_FW_VERSION, hr_fw_version_r, NULL},
        {HR_BOOT_VERSION, HR_BOOT_VERSION, hr_boot_version_r, NULL},
        {HR_DEVICE_MODE, HR_DEVICE_MODE, hr_device_mode_r, NULL},
        {HR_APP_BUILD_HI, HR_APP_BUILD_HI, hr_build_hi_r, NULL},
        {HR_APP_BUILD_LO, HR_APP_BUILD_LO, hr_build_lo_r, NULL},
        {HR_UPDATE_COMMAND, HR_UPDATE_COMMAND, hr_update_cmd_r, hr_update_cmd_w},
};

#define HR_TABLE_SIZE (sizeof(hr_table) / sizeof(hr_table[0]))

bool registers_read_hr(const struct app_registers *regs, uint16_t hr, uint16_t *value) {
    if (config_registers_hr_read(hr, value)) {
        return true;
    }
    for (size_t i = 0; i < HR_TABLE_SIZE; i++) {
        if (hr >= hr_table[i].min && hr <= hr_table[i].max) {
            return hr_table[i].read ? hr_table[i].read(regs, hr, value) : false;
        }
    }
    return false;
}

bool registers_write_hr(struct app_registers *regs, uint16_t hr, uint16_t value) {
    if (hr >= DDM_HR_CONFIG_COMMAND && hr <= 5399u) {
        return config_registers_hr_write(hr, value);
    }
    for (size_t i = 0; i < HR_TABLE_SIZE; i++) {
        if (hr >= hr_table[i].min && hr <= hr_table[i].max) {
            return hr_table[i].write ? hr_table[i].write(regs, hr, value) : false;
        }
    }
    return false;
}
