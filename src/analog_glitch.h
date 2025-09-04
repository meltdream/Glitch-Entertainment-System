#ifndef ANALOG_GLITCH_H
#define ANALOG_GLITCH_H

#include <stdint.h>

#ifdef __cplusplus
// C++ has native bool
#else
// C code - include nofrendo types
#include "nofrendo/noftypes.h"
#endif

// Configuration
#define GLITCH_POT_PIN 36  // GPIO 36 (ADC1_CHANNEL_0)
#define GLITCH_SLOTS 10
#define GLITCH_DEADZONE 100  // ADC value threshold for "off" position
#define GLITCH_MAX_PINS 9    // Pins 0-8 for glitching
#define GLITCH_UPDATE_INTERVAL_MS 50  // Debounce/update rate limiting

// State tracking
struct analog_glitch_state {
    uint16_t raw_adc;
    uint8_t current_slot;
    uint8_t last_slot;
    uint32_t last_update_ms;
    bool enabled;
};

// Public interface
#ifdef __cplusplus
extern "C" {
#endif

void analog_glitch_init();
void analog_glitch_update();
uint8_t get_glitch_slot();
bool is_glitch_enabled();
void set_glitch_enabled(bool enabled);

// PPU glitch function - applies analog-controlled glitch to address
uint16_t analog_ppu_glitch_addr(uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif // ANALOG_GLITCH_H