#include "analog_glitch.h"

#ifdef ESP_PLATFORM
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "Arduino.h"

extern "C" {

static analog_glitch_state _glitch_state = {0};
static uint16_t _glitch_mask = 0x0000;  // Bitmask for which pins to glitch
static uint32_t _debug_call_count = 0;  // Debug counter

void analog_glitch_init() {
    // Configure ADC for potentiometer reading
    adc1_config_width(ADC_WIDTH_BIT_12);  // 12-bit resolution (0-4095)
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);  // GPIO 36
    
    _glitch_state.enabled = true;
    _glitch_state.current_slot = 0;
    _glitch_state.last_slot = 0;
    _glitch_state.last_update_ms = 0;
    
    printf("Analog glitch system initialized on GPIO 36\n");
    
    // Test ADC reading immediately
    int test_adc = adc1_get_raw(ADC1_CHANNEL_0);
    printf("Initial ADC test read: %d\n", test_adc);
}

static void update_glitch_mask(uint8_t slot) {
    if (slot == 0) {
        // Slot 0 (deadzone) - no glitching
        _glitch_mask = 0x0000;
    } else if (slot <= GLITCH_MAX_PINS) {
        // Create mask for only the specific pin number (slot-1)
        // Example: slot=3 creates mask 0x0004 (only pin 2)
        _glitch_mask = 1 << (slot - 1);
    } else {
        // Safety: don't exceed max pins
        _glitch_mask = 1 << (GLITCH_MAX_PINS - 1);
    }
}

void analog_glitch_update() {
    _debug_call_count++;
    
    // Print debug info every 100 calls to show it's working (reduced from 1000)
    if (_debug_call_count % 100 == 0) {
        printf("Analog glitch update called %d times\n", _debug_call_count);
    }
    
    // Also print first few calls
    if (_debug_call_count <= 5) {
        printf("Analog glitch update #%d\n", _debug_call_count);
    }
    
    if (!_glitch_state.enabled) {
        return;
    }
    
    uint32_t current_ms = millis();
    
    // Rate limiting to avoid constant updates
    if (current_ms - _glitch_state.last_update_ms < GLITCH_UPDATE_INTERVAL_MS) {
        return;
    }
    
    // Read ADC value
    int adc_raw = adc1_get_raw(ADC1_CHANNEL_0);
    if (adc_raw < 0) {
        return;  // ADC read failed
    }
    
    _glitch_state.raw_adc = (uint16_t)adc_raw;
    
    // Quantize into slots
    uint8_t new_slot;
    if (_glitch_state.raw_adc < GLITCH_DEADZONE) {
        // Deadzone - no glitching
        new_slot = 0;
    } else {
        // Map remaining range (GLITCH_DEADZONE to 4095) to slots 1-9
        uint16_t active_range = 4095 - GLITCH_DEADZONE;
        uint16_t adjusted_value = _glitch_state.raw_adc - GLITCH_DEADZONE;
        new_slot = (adjusted_value * (GLITCH_MAX_PINS - 1)) / active_range + 1;
        
        // Ensure we don't exceed max slot
        if (new_slot > GLITCH_MAX_PINS) {
            new_slot = GLITCH_MAX_PINS;
        }
    }
    
    // Only update if slot changed
    if (new_slot != _glitch_state.last_slot) {
        _glitch_state.current_slot = new_slot;
        _glitch_state.last_slot = new_slot;
        update_glitch_mask(new_slot);
        
        // Debug print when slot changes (non-flooding)
        printf("ADC: %d -> Glitch Slot: %d (mask: 0x%04X)\n", 
               _glitch_state.raw_adc, new_slot, _glitch_mask);
    }
    
    _glitch_state.last_update_ms = current_ms;
}

uint8_t get_glitch_slot() {
    return _glitch_state.current_slot;
}

bool is_glitch_enabled() {
    return _glitch_state.enabled;
}

void set_glitch_enabled(bool enabled) {
    _glitch_state.enabled = enabled;
    if (!enabled) {
        _glitch_mask = 0x0000;  // Disable all glitching
    }
}

// Function to be used by PPU glitch system
uint16_t analog_ppu_glitch_addr(uint16_t addr) {
    if (!_glitch_state.enabled || _glitch_mask == 0) {
        return addr;  // No glitching
    }
    
    // Apply the mask to force selected address lines low
    return addr & ~_glitch_mask;
}

} // extern "C"

#else
// Non-ESP platform stubs

extern "C" {

static analog_glitch_state _glitch_state = {0};

void analog_glitch_init() {
    _glitch_state.enabled = false;
}

void analog_glitch_update() {}

uint8_t get_glitch_slot() {
    return 0;
}

bool is_glitch_enabled() {
    return false;
}

void set_glitch_enabled(bool enabled) {
    (void)enabled;
}

uint16_t analog_ppu_glitch_addr(uint16_t addr) {
    return addr;
}

} // extern "C"

#endif // ESP_PLATFORM