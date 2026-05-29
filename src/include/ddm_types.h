#ifndef DDM_TYPES_H
#define DDM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "../common/ddm_config.h"

#define DDM_OUTPUT_COUNT       DDM_CHANNEL_COUNT
#define DDM_ADC_DI_COUNT       8u
#define DDM_BUTTON_BINDING_MAX DDM_MAX_BUTTON_BINDINGS

#define DDM_LEVEL_MIN 0u
#define DDM_LEVEL_MAX 1023u
#define DDM_DI_NONE   0xffu

struct ddm_input_snapshot {
    uint16_t adc_raw[DDM_ADC_DI_COUNT];
    bool raw_active[DDM_DI_COUNT];
    bool debounced_pressed[DDM_DI_COUNT];
    uint32_t last_change_ms[DDM_DI_COUNT];
    uint32_t stable_since_ms[DDM_DI_COUNT];
    uint32_t press_started_ms[DDM_DI_COUNT];
};

#endif
