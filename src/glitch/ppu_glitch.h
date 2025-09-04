#ifndef PPU_GLITCH_H
#define PPU_GLITCH_H

#include <stdint.h>

/* Glitch control - change to 1 to enable, 0 to disable */
/* Note: Super Mario Bros 3 crash issue persists even with PPU_GLITCH_ENABLED 0 */
#define PPU_GLITCH_ENABLED 1

/* Include analog glitch system for potentiometer control */
#include "../analog_glitch.h"

/* Apply simulated hardware faults to a PPU address.
   Now uses analog potentiometer control instead of fixed glitch. */
static inline uint16_t ppu_glitch_addr(uint16_t addr)
{
#if PPU_GLITCH_ENABLED
    return analog_ppu_glitch_addr(addr);
#else
    return addr; /* glitch disabled - return address unchanged */
#endif
}

#endif /* PPU_GLITCH_H */
