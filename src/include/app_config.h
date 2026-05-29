#ifndef DDM_APP_CONFIG_H
#define DDM_APP_CONFIG_H

#include "registers.h"

void app_config_init(void);
void app_config_load_or_defaults(struct app_registers *regs);
bool app_config_save(const struct app_registers *regs);
bool app_config_reload(struct app_registers *regs);
void app_config_factory_defaults(struct app_registers *regs);

#endif
