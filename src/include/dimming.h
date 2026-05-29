#ifndef DDM_DIMMING_H
#define DDM_DIMMING_H

#include <stdint.h>

void dimming_setup(void);
void dimming_set_channel_profile(unsigned int channel, uint16_t min_level, uint16_t max_level, uint16_t output_curve);
void dimming_set_channel_level(unsigned int channel, uint16_t level, uint16_t fade_ms);
void dimming_poll(uint32_t now_ms);

#endif
