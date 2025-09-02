#ifndef PPU_GLITCH_H
#define PPU_GLITCH_H

#include <stdint.h>

/* Glitch control - change to 1 to enable, 0 to disable */
#define PPU_GLITCH_ENABLED 1

/* Apply simulated hardware faults to a PPU address.
   Example: A07 stuck low forces bit 7 to 0. */
static inline uint16_t ppu_glitch_addr(uint16_t addr)
{
#if PPU_GLITCH_ENABLED
    return addr & ~0x0080; /* force A07 = 0 */
#else
    return addr; /* glitch disabled - return address unchanged */
#endif
}

#endif /* PPU_GLITCH_H */
