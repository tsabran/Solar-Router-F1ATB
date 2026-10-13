#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>

// -----------------------------------------------------------------------------
// Emulation des types et fonctions Arduino pour ESP32
// -----------------------------------------------------------------------------

using byte = uint8_t;

inline float constrain(float x, float a, float b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

inline int constrain(int x, int a, int b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

// -----------------------------------------------------------------------------
// Temps simulé
// -----------------------------------------------------------------------------
inline unsigned long g_simulated_millis = 1000;

inline unsigned long millis() {
    return g_simulated_millis;
}

inline void set_simulated_millis(unsigned long ms) {
    g_simulated_millis = ms;
}

inline void advance_simulated_millis(unsigned long deltaMs) {
    g_simulated_millis += deltaMs;
}

inline void delay(unsigned long ms) {
    g_simulated_millis += ms;
}

inline void yield() {}

// -----------------------------------------------------------------------------
// Classe Arduino String émulée
// -----------------------------------------------------------------------------
class String {
private:
    std::string _data;

public:
    String() : _data("") {}
    String(const char* s) : _data(s ? s : "") {}
    String(const char* s, unsigned int length) : _data(s ? std::string(s, length) : "") {}
    String(const std::string& s) : _data(s) {}
    String(char c) : _data(1, c) {}
    String(int v) : _data(std::to_string(v)) {}
    String(unsigned int v) : _data(std::to_string(v)) {}
    String(long v) : _data(std::to_string(v)) {}
    String(unsigned long v) : _data(std::to_string(v)) {}
    String(float v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", v);
        _data = buf;
    }
    String(double v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", v);
        _data = buf;
    }
    String(double v, unsigned int decimals) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", int(decimals), v);
        _data = buf;
    }
    size_t write(uint8_t value) {
        _data.push_back(char(value));
        return 1;
    }
    size_t write(const uint8_t* data, size_t size) {
        _data.append(reinterpret_cast<const char*>(data), size);
        return size;
    }

    size_t length() const { return _data.length(); }
    bool reserve(unsigned int size) { _data.reserve(size); return true; }
    const char* c_str() const { return _data.c_str(); }

    void toCharArray(char* buffer, size_t size) const {
        if (size == 0) return;
        const size_t count = std::min(size - 1, _data.size());
        std::memcpy(buffer, _data.data(), count);
        buffer[count] = '\0';
    }

    int indexOf(const String& s, int fromIndex = 0) const {
        if (fromIndex < 0) fromIndex = 0;
        size_t pos = _data.find(s._data, (size_t)fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    int indexOf(const char* s, int fromIndex = 0) const {
        if (fromIndex < 0) fromIndex = 0;
        size_t pos = _data.find(s ? s : "", (size_t)fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    int indexOf(char c, int fromIndex = 0) const {
        if (fromIndex < 0) fromIndex = 0;
        size_t pos = _data.find(c, (size_t)fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    String substring(int start, int end = -1) const {
        if (start < 0) start = 0;
        if ((size_t)start >= _data.length()) return String("");
        if (end < 0 || (size_t)end > _data.length()) {
            return String(_data.substr((size_t)start));
        }
        if (end <= start) return String("");
        return String(_data.substr((size_t)start, (size_t)(end - start)));
    }

    float toFloat() const {
        try {
            return std::stof(_data);
        } catch (...) {
            return 0.0f;
        }
    }

    int toInt() const {
        try {
            return std::stoi(_data);
        } catch (...) {
            return 0;
        }
    }

    void trim() {
        size_t first = _data.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            _data = "";
            return;
        }
        size_t last = _data.find_last_not_of(" \t\r\n");
        _data = _data.substr(first, (last - first + 1));
    }

    String& operator+=(const String& rhs) {
        _data += rhs._data;
        return *this;
    }
    String& operator+=(const char* rhs) {
        if (rhs) _data += rhs;
        return *this;
    }
    char charAt(unsigned int i) const { return i < _data.size() ? _data[i] : 0; }
    String& operator+=(char c) {
        _data += c;
        return *this;
    }

    bool operator==(const String& rhs) const { return _data == rhs._data; }
    bool operator==(const char* rhs) const { return _data == (rhs ? rhs : ""); }
    bool operator!=(const String& rhs) const { return _data != rhs._data; }
    bool operator!=(const char* rhs) const { return _data != (rhs ? rhs : ""); }

    operator std::string() const { return _data; }
};

inline bool operator==(const char* lhs, const String& rhs) {
    return rhs == lhs;
}
inline bool operator!=(const char* lhs, const String& rhs) {
    return rhs != lhs;
}

inline String operator+(const String& lhs, const String& rhs) {
    return String(std::string(lhs.c_str()) + std::string(rhs.c_str()));
}
inline String operator+(const String& lhs, const char* rhs) {
    return String(std::string(lhs.c_str()) + (rhs ? rhs : ""));
}
inline String operator+(const char* lhs, const String& rhs) {
    return String((lhs ? lhs : "") + std::string(rhs.c_str()));
}
inline std::ostream& operator<<(std::ostream& os, const String& s) {
    return os << s.c_str();
}

// -----------------------------------------------------------------------------
// IPAddress émulée
// -----------------------------------------------------------------------------
class IPAddress {
public:
    uint8_t bytes[4]{192, 168, 1, 100};
    IPAddress() {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        bytes[0] = a; bytes[1] = b; bytes[2] = c; bytes[3] = d;
    }
    uint8_t operator[](int i) const { return bytes[i]; }
    uint8_t& operator[](int i) { return bytes[i]; }
    String toString() const {
        return String(std::to_string(bytes[0]) + "." + std::to_string(bytes[1]) + "." +
                      std::to_string(bytes[2]) + "." + std::to_string(bytes[3]));
    }
};

inline String IP2String(unsigned long IP) {
    byte b0 = IP & 0xFF;
    byte b1 = (IP >> 8) & 0xFF;
    byte b2 = (IP >> 16) & 0xFF;
    byte b3 = (IP >> 24) & 0xFF;
    return String(std::to_string(b3) + "." + std::to_string(b2) + "." + std::to_string(b1) + "." + std::to_string(b0));
}

// -----------------------------------------------------------------------------
// NetworkClient / NetworkClientSecure émulé
// -----------------------------------------------------------------------------
class NetworkClientSecure {
private:
    std::deque<char> _rxBuffer;
    bool _connected = false;
    unsigned long _timeout = 1000;
    bool _simulateTimeoutOnRead = false;

public:
    void setInsecure() {}
    void setTimeout(unsigned long timeoutMs) { _timeout = timeoutMs; }

    bool connect(const char* host, uint16_t port, uint32_t timeout = 3000) {
        (void)host; (void)port; (void)timeout;
        _connected = true;
        return true;
    }

    bool connected() const { return _connected; }
    void stop() {
        _connected = false;
        _rxBuffer.clear();
    }

    void feedData(const std::string& data) {
        for (char c : data) {
            _rxBuffer.push_back(c);
        }
        _connected = true;
    }

    void setSimulateTimeout(bool enable) {
        _simulateTimeoutOnRead = enable;
    }

    int available() const { return (int)_rxBuffer.size(); }

    char read() {
        if (_rxBuffer.empty()) return -1;
        char c = _rxBuffer.front();
        _rxBuffer.pop_front();
        return c;
    }

    String readStringUntil(char terminator) {
        if (_simulateTimeoutOnRead) {
            advance_simulated_millis(_timeout + 10);
            return String("");
        }

        std::string res;
        bool found = false;
        while (!_rxBuffer.empty()) {
            char c = _rxBuffer.front();
            _rxBuffer.pop_front();
            if (c == terminator) {
                found = true;
                break;
            }
            res += c;
        }

        if (!found) {
            // Sur ESP32, si le caractère n'est pas reçu, le client attend jusqu'au timeout
            advance_simulated_millis(_timeout + 10);
        }

        return String(res);
    }

    void println(const String& s = "") { (void)s; }
    void println(const char* s) { (void)s; }
    void print(const String& s) { (void)s; }
};

using NetworkClient = NetworkClientSecure;

// -----------------------------------------------------------------------------
// Fonctions de log & Serial
// -----------------------------------------------------------------------------
inline std::vector<String> g_stockMessages;
inline void StockMessage(String m) {
    g_stockMessages.push_back(m);
}

inline void TelnetPrintln(String m) { (void)m; }
inline void TelnetPrint(String m) { (void)m; }

struct MockSerial {
    void write(char c) { (void)c; }
    void print(const String& s) { (void)s; }
    void println(const String& s = "") { (void)s; }
};
inline MockSerial Serial;
