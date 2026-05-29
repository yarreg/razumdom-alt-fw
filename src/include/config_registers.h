#ifndef DDM_CONFIG_REGISTERS_H
#define DDM_CONFIG_REGISTERS_H

#include "buttons.h"

#define DDM_HR_CONFIG_COMMAND         5000u
#define DDM_HR_CONFIG_STATUS          5001u
#define DDM_HR_CONFIG_BINDING_COUNT   5002u
#define DDM_HR_CONFIG_DI_ADC_THRESHOLD 5003u
#define DDM_HR_CONFIG_DEBOUNCE_MS     5004u
#define DDM_HR_CONFIG_SHORT_MIN_MS    5005u
#define DDM_HR_CONFIG_SHORT_MAX_MS    5006u
#define DDM_HR_CONFIG_LONG_MS         5007u
#define DDM_HR_CONFIG_AUTO_LIGHT_SUPPRESS_S 5008u
#define DDM_HR_CHANNEL_PROFILE_BASE   5010u
#define DDM_HR_CHANNEL_PROFILE_STRIDE 16u
#define DDM_HR_CONFIG_BINDING_BASE    5100u
#define DDM_HR_CONFIG_BINDING_STRIDE  32u

enum ddm_config_register_status {
    DDM_CONFIG_REG_OK = 0,
    DDM_CONFIG_REG_ERR_RANGE = 1,
    DDM_CONFIG_REG_ERR_TYPE = 2,
    DDM_CONFIG_REG_ERR_DI = 3,
    DDM_CONFIG_REG_ERR_DUP_DI = 4,
    DDM_CONFIG_REG_ERR_LEVEL = 5,
    DDM_CONFIG_REG_ERR_RESERVED = 6,
};

enum ddm_config_register_command {
    DDM_CONFIG_CMD_NONE = 0,
    DDM_CONFIG_CMD_SAVE = 1,
    DDM_CONFIG_CMD_RELOAD = 2,
    DDM_CONFIG_CMD_FACTORY_DEFAULTS = 3,
};

void config_registers_init(void);
const struct ddm_config *config_registers_config(void);
bool config_registers_apply_config(const struct ddm_config *config);
enum ddm_config_register_command config_registers_take_command(void);
uint16_t config_registers_status(void);
bool config_registers_hr_read(uint16_t hr, uint16_t *value);
bool config_registers_hr_write(uint16_t hr, uint16_t value);

#endif
