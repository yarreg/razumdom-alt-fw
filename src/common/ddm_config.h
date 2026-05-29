#ifndef DDM_CONFIG_H
#define DDM_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DDM_CHANNEL_COUNT       4u
#define DDM_DI_COUNT            9u
#define DDM_MAX_BUTTON_BINDINGS 8u

#define DDM_FIRMWARE_VERSION           0x0001u
#define DDM_BOOTLOADER_VERSION_UNKNOWN 0x0000u

#define DDM_MODBUS_DEFAULT_ADDRESS 34u
#define DDM_MODBUS_BAUD_9600       0u
#define DDM_MODBUS_BAUD_19200      1u
#define DDM_MODBUS_BAUD_38400      2u
#define DDM_MODBUS_BAUD_57600      3u
#define DDM_MODBUS_BAUD_115200     4u
#define DDM_MODBUS_PARITY_NONE     0u
#define DDM_MODBUS_PARITY_EVEN     1u
#define DDM_MODBUS_PARITY_ODD      2u
#define DDM_MODBUS_STOP_1          1u
#define DDM_MODBUS_STOP_2          2u
#define DDM_MODBUS_DEFAULT_PORT_CFG                                                                                    \
    DDM_MODBUS_PORT_CFG(DDM_MODBUS_BAUD_57600, DDM_MODBUS_PARITY_NONE, DDM_MODBUS_STOP_2)

#define DDM_MODBUS_PORT_CFG(baud_code, parity_code, stop_bits)                                                         \
    ((uint16_t)(((baud_code) & 0x0fu) | (((parity_code) & 0x03u) << 4) | ((((stop_bits) - 1u) & 0x03u) << 6)))

static inline bool ddm_modbus_port_cfg_valid(uint16_t port_cfg) {
    uint16_t baud_code = port_cfg & 0x0fu;
    uint16_t parity_code = (port_cfg >> 4) & 0x03u;
    uint16_t stop_bits = (uint16_t)(((port_cfg >> 6) & 0x03u) + 1u);

    return baud_code <= DDM_MODBUS_BAUD_115200 && parity_code <= DDM_MODBUS_PARITY_ODD
            && (stop_bits == DDM_MODBUS_STOP_1 || stop_bits == DDM_MODBUS_STOP_2) && (port_cfg & 0xff00u) == 0u;
}

#define DDM_DEFAULT_DI_ADC_THRESHOLD 1000u
#define DDM_DEFAULT_DEBOUNCE_MS    30u
#define DDM_DEFAULT_SHORT_MIN_MS   50u
#define DDM_DEFAULT_SHORT_MAX_MS   700u
#define DDM_DEFAULT_LONG_MS        800u
#define DDM_DEFAULT_AUTO_LIGHT_SUPPRESS_S 15u
#define DDM_MAX_AUTO_LIGHT_SUPPRESS_S     3600u

enum ddm_button_type {
    DDM_BUTTON_DISABLED = 0,
    DDM_BUTTON_ROCKER_UP_DOWN = 1,
    DDM_BUTTON_LATCHING_SWITCH = 2,
    DDM_BUTTON_MOMENTARY_PUSH = 3,
};

struct ddm_dimming_zone {
    uint16_t min_level;
    uint16_t max_level;
};

enum ddm_binding_target_type {
    DDM_BINDING_TARGET_CHANNEL = 1,
    DDM_BINDING_TARGET_GROUP = 2,
};

#define DDM_GROUP_ALL_CHANNELS 1u

struct ddm_channel_profile {
    struct ddm_dimming_zone zone;
    uint16_t default_level;
    uint16_t night_level;
    uint16_t fade_on_ms;
    uint16_t fade_off_ms;
    uint16_t output_curve;
};

enum ddm_output_curve {
    DDM_OUTPUT_CURVE_LINEAR = 0,
    DDM_OUTPUT_CURVE_GAMMA_0_5 = 1,
    DDM_OUTPUT_CURVE_GAMMA_0_7 = 2,
    DDM_OUTPUT_CURVE_GAMMA_1_5 = 3,
    DDM_OUTPUT_CURVE_GAMMA_2_0 = 4,
    DDM_OUTPUT_CURVE_GAMMA_2_5 = 5,
    DDM_OUTPUT_CURVE_GAMMA_3_0 = 6,
    DDM_OUTPUT_CURVE_MAX = DDM_OUTPUT_CURVE_GAMMA_3_0,
};

struct ddm_button_binding {
    uint8_t enabled;
    uint8_t type;
    uint8_t target_type;
    uint8_t target_id;
    uint8_t di_primary;
    uint8_t di_secondary;
    uint16_t hold_step;
    uint16_t hold_period_ms;
    uint16_t flags;
};

struct ddm_config {
    uint16_t modbus_address;
    uint16_t modbus_port_cfg;
    uint16_t binding_count;
    uint16_t di_adc_active_threshold;
    uint16_t debounce_ms;
    uint16_t short_press_min_ms;
    uint16_t short_press_max_ms;
    uint16_t long_press_ms;
    struct ddm_channel_profile channels[DDM_CHANNEL_COUNT];
    struct ddm_button_binding bindings[DDM_MAX_BUTTON_BINDINGS];
    uint16_t auto_light_suppress_s;
};

struct ddm_channel_state {
    uint8_t on;
    uint8_t brightness_percent_setpoint;
    uint16_t brightness_level;
    uint16_t last_nonzero_level;
};

struct ddm_runtime_state {
    struct ddm_channel_state channels[DDM_CHANNEL_COUNT];
    uint16_t config_status;
};

enum ddm_config_status {
    DDM_CONFIG_STATUS_OK = 0,
    DDM_CONFIG_STATUS_INVALID_VALUE = 1,
    DDM_CONFIG_STATUS_FLASH_READ_FAILED = 2,
    DDM_CONFIG_STATUS_FLASH_WRITE_FAILED = 3,
    DDM_CONFIG_STATUS_FLASH_EMPTY = 4,
};

void ddm_config_set_defaults(struct ddm_config *cfg);
void ddm_runtime_set_defaults(struct ddm_runtime_state *rt);
bool ddm_config_validate(const struct ddm_config *cfg, uint16_t *status);

uint16_t ddm_percent_to_level(uint16_t percent);
uint16_t ddm_level_to_percent(uint16_t level);

#endif
