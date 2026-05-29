#ifndef DDM_TIMEBASE_H
#define DDM_TIMEBASE_H

#include <stdint.h>

void timebase_setup(void);
uint32_t timebase_ms(void);
void timebase_delay_ms(uint32_t ms);

#endif
