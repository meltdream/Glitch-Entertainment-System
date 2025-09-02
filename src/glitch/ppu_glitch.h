#ifndef PPU_GLITCH_H
#define PPU_GLITCH_H

#include <stdint.h>

/* Apply simulated hardware faults to a PPU address.
   Example: A04 stuck low forces bit 4 to 0. */
static inline uint16_t ppu_glitch_addr(uint16_t addr)
{
    return addr & ~0x0010; /* force A04 = 0 */
}

#endif /* PPU_GLITCH_H */
