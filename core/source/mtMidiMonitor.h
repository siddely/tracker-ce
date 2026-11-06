#ifndef SOURCE_MTMIDIMONITOR_H_
#define SOURCE_MTMIDIMONITOR_H_

#include <stdint.h>

// MIDI monitor ("verbose" mode): when enabled in Config > MIDI, incoming MIDI messages are
// shown briefly on screen with their source, so you can check what a controller sends.
// MIDI clock is not shown (it would flood the display); transport and everything else is.

enum class MidiMonitorSource : uint8_t
{
    kJack,
    kUsb,
};

// Record an incoming message. Cheap; does nothing when the monitor is off.
void midiMonitorLog(MidiMonitorSource source,
                    uint8_t status,
                    uint8_t channel,
                    uint8_t data1,
                    uint8_t data2);

// Shows the latest messages, at most a few times per second. Called from the interface loop.
void midiMonitorUpdate();

#endif /* SOURCE_MTMIDIMONITOR_H_ */
