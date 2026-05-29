#ifndef DDM_BUTTONS_H
#define DDM_BUTTONS_H

#include "ddm_types.h"
#include "inputs.h"

void buttons_init(const struct ddm_config *config);
void buttons_set_config(const struct ddm_config *config);
const struct ddm_config *buttons_config(void);

void buttons_on_input_event(const struct ddm_input_event *event, uint32_t now_ms);
void buttons_tick(uint32_t now_ms);

#endif
