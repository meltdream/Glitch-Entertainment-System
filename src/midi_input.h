#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include <stdint.h>

void midi_input_init();
int get_hid_midi(uint8_t* dst);

#endif // MIDI_INPUT_H
