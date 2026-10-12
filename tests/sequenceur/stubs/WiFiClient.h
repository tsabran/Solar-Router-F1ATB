#pragma once
#include "Arduino.h"
#include <stdexcept>

class WiFiClient {
public:
    bool connect(const char*, int, int) {
        SequencerIO::networkCalls++;
        throw std::logic_error("Unexpected external action in sequencer test");
    }
    void stop() {}
    void print(const String&) {}
    int available() { return 0; }
    String readStringUntil(char) { return ""; }
};
