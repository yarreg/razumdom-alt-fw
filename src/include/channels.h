#ifndef DDM_CHANNELS_H
#define DDM_CHANNELS_H

#include "ddm_types.h"

struct ddm_channel_runtime {
    uint16_t level;
    uint16_t last_nonzero_level;
    uint8_t percent_setpoint;
    bool has_last_nonzero;
    bool auto_active;
    bool on_from_rocker_down_min;
    int8_t momentary_ramp_dir;
    uint32_t ramp_due_ms;
    uint32_t auto_suppressed_until_ms;
};

void channels_init(const struct ddm_config *config);
void channels_set_config(const struct ddm_config *config);

const struct ddm_channel_profile *channels_profile(uint8_t channel);
const struct ddm_channel_runtime *channels_state(uint8_t channel);
struct ddm_channel_runtime *channels_state_mut(uint8_t channel);

uint16_t channels_level(uint8_t channel);
uint8_t channels_percent(uint8_t channel);
bool channels_on(uint8_t channel);
bool channels_auto_active(uint8_t channel);
uint16_t channels_auto_level(uint8_t channel);
uint8_t channels_auto_percent(uint8_t channel);

uint16_t channels_clamp_level(uint8_t channel, uint16_t level);
uint16_t channels_startup_level(uint8_t channel);
uint16_t channels_night_level(uint8_t channel);
uint16_t channels_transition_fade_ms(uint8_t channel, uint16_t from_level, uint16_t to_level);

void channels_remember_nonzero(uint8_t channel);
void channels_set_on_from_rocker_down_min(uint8_t channel, bool value);
void channels_apply_level(uint8_t channel, uint16_t level, uint16_t fade_ms);
void channels_apply_level_with_profile_fade(uint8_t channel, uint16_t level);
void channels_apply_all_level(uint16_t level, bool use_profile_fade);
void channels_turn_all_off(void);
bool channels_apply_auto_level_with_profile_fade(uint8_t channel, uint16_t level);

uint16_t channels_percent_to_level(uint16_t percent);
uint8_t channels_level_to_percent(uint16_t level);
uint16_t channels_percent_to_level_for_channel(uint8_t channel, uint16_t percent);
uint8_t channels_level_to_percent_for_channel(uint8_t channel, uint16_t level);

#endif
