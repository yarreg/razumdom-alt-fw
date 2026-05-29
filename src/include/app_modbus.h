#ifndef DDM_APP_MODBUS_H
#define DDM_APP_MODBUS_H

#include "registers.h"

void app_modbus_init(struct app_registers *registers);
void app_modbus_poll(void);
bool app_modbus_tx_idle(void);

#endif
