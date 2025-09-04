#ifndef GPIO_INPUT_H
#define GPIO_INPUT_H

#include <stdint.h>

// GPIO pin definitions for buttons
// These are raw GPIO numbers to avoid depending on ESP-IDF headers
// Note: GPIO 18 is used for audio output, GPIO 0 for IR, GPIO 25 for video
#define GPIO_BTN_A      21
#define GPIO_BTN_B      22
#define GPIO_BTN_SELECT 23
#define GPIO_BTN_START  19
#define GPIO_BTN_UP     4   // Changed from 18 to avoid audio conflict
#define GPIO_BTN_DOWN   5
#define GPIO_BTN_LEFT   17
#define GPIO_BTN_RIGHT  16

// Button state tracking
struct gpio_button_state {
    uint16_t current_state;
    uint16_t last_state;
    uint8_t debounce_timer;
};

void gpio_input_init();
int get_hid_gpio(uint8_t* dst);

#endif // GPIO_INPUT_H
