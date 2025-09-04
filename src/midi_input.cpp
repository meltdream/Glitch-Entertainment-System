#include "midi_input.h"
#include "emu.h"
#include "analog_glitch.h"

#ifdef ESP_PLATFORM
#include <Arduino.h>

#define MIDI_RX_PIN 26
#define MIDI_GLITCH_CC 1

struct midi_button_state {
    uint16_t current_state;
    uint16_t last_state;
};

static midi_button_state _midi_buttons = {0};
static uint8_t _running_status = 0;
static uint8_t _data[2];
static uint8_t _data_index = 0;

static const uint8_t note_map[8] = {60,62,64,65,67,69,71,72};
static const uint16_t button_map[8] = {
    GENERIC_UP,
    GENERIC_DOWN,
    GENERIC_LEFT,
    GENERIC_RIGHT,
    GENERIC_FIRE_A,
    GENERIC_FIRE_B,
    GENERIC_START,
    GENERIC_SELECT
};

static void handle_note(uint8_t note, bool on) {
    for (int i = 0; i < 8; ++i) {
        if (note_map[i] == note) {
            if (on)
                _midi_buttons.current_state |= button_map[i];
            else
                _midi_buttons.current_state &= ~button_map[i];
            break;
        }
    }
}

static void handle_cc(uint8_t cc, uint8_t val) {
    if (cc == MIDI_GLITCH_CC) {
        uint8_t slot = (val * (GLITCH_SLOTS - 1)) / 127;
        analog_glitch_set_slot(slot);
    }
}

static void midi_parse(uint8_t b) {
    if (b & 0x80) {
        _running_status = b;
        _data_index = 0;
        return;
    }
    if (!_running_status)
        return;
    _data[_data_index++] = b;
    if (_data_index >= 2) {
        uint8_t status = _running_status & 0xF0;
        if (status == 0x90 || status == 0x80) {
            bool on = (status == 0x90) && _data[1] > 0;
            handle_note(_data[0], on);
        } else if (status == 0xB0) {
            handle_cc(_data[0], _data[1]);
        }
        _data_index = 0;
    }
}

static void midi_poll() {
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        midi_parse(b);
    }
}

void midi_input_init() {
    Serial1.begin(31250, SERIAL_8N1, MIDI_RX_PIN, -1);
}

int get_hid_midi(uint8_t* dst) {
    midi_poll();
    if (_midi_buttons.current_state != _midi_buttons.last_state) {
        _midi_buttons.last_state = _midi_buttons.current_state;
        dst[0] = 0xA1;
        dst[1] = 0x42;
        dst[2] = _midi_buttons.current_state & 0xFF;
        dst[3] = _midi_buttons.current_state >> 8;
        dst[4] = 0;
        dst[5] = 0;
        return 6;
    }
    return 0;
}

#else

void midi_input_init() {}
int get_hid_midi(uint8_t* dst) { (void)dst; return 0; }

#endif // ESP_PLATFORM
