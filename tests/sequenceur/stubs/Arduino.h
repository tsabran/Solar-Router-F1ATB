#pragma once
#include "../../harness/mock_arduino.h"

namespace SequencerIO {
struct Write { int pin; int value; };
inline std::vector<Write> digital, pwm, modes, attachments;
inline int networkCalls = 0;
}

constexpr int OUTPUT = 1;
inline void digitalWrite(int pin, int value) { SequencerIO::digital.push_back({pin, value}); }
inline void ledcWrite(int pin, int value) { SequencerIO::pwm.push_back({pin, value}); }
inline void pinMode(int pin, int mode) { SequencerIO::modes.push_back({pin, mode}); }
inline void ledcAttachChannel(int pin, int frequency, int, int) {
    SequencerIO::attachments.push_back({pin, frequency});
}
