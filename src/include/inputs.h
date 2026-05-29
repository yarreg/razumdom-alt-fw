#ifndef DDM_INPUTS_H
#define DDM_INPUTS_H

#include "ddm_types.h"

enum ddm_input_event_type {
    DDM_INPUT_EVENT_NONE = 0,
    DDM_INPUT_EVENT_RAW_CHANGE,
    DDM_INPUT_EVENT_DEBOUNCED_PRESS,
    DDM_INPUT_EVENT_DEBOUNCED_RELEASE,
};

struct ddm_input_event {
    enum ddm_input_event_type type;
    uint8_t di;
    bool raw_active;
    bool debounced_pressed;
    uint32_t stable_ms;
    uint32_t duration_ms;
};

void inputs_init(uint16_t di_adc_active_threshold, uint16_t debounce_ms);
void inputs_set_thresholds(uint16_t di_adc_active_threshold, uint16_t debounce_ms);
void inputs_scan(uint32_t now_ms);
bool inputs_pop_event(struct ddm_input_event *event);
const struct ddm_input_snapshot *inputs_snapshot(void);

bool inputs_di_pressed(uint8_t di);
bool inputs_di_raw_active(uint8_t di);

#endif
