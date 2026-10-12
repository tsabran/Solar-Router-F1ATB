#pragma once

namespace EnvoyFirmware {

struct Reply {
    std::string data;
    bool keepAlive = true;
    unsigned int intervalMs = 0;
    size_t chunkSize = 4096;
};
struct Connection { std::string host; uint16_t port; };
inline std::deque<Reply> envoyReplies, cloudReplies;
inline std::deque<bool> connectionResults;
inline std::vector<Connection> connections;
inline std::vector<std::string> requests;
inline unsigned int generation = 0;

class NetworkClient {
public:
    void setInsecure() {}
    void setTimeout(unsigned long) {}
    bool connect(const char* host, uint16_t port, uint32_t = 3000) {
        synchronize();
        connections.push_back({host, port});
        host_ = host;
        port_ = port;
        connected_ = true;
        if (!connectionResults.empty()) {
            connected_ = connectionResults.front();
            connectionResults.pop_front();
        }
        if (connected_ && host_.find("enphaseenergy.com") != std::string::npos) {
            if (cloudReplies.empty()) throw std::logic_error("Unexpected cloud connection");
            load(cloudReplies.front());
            cloudReplies.pop_front();
        }
        return connected_;
    }
    bool connected() {
        synchronize();
        return connected_ && (reply_.keepAlive || offset_ < reply_.data.size());
    }
    int available() {
        synchronize();
        if (!connected_ || millis() < nextChunkAt_) return 0;
        return int(std::min(reply_.chunkSize - consumedInChunk_, reply_.data.size() - offset_));
    }
    int read() {
        if (!available()) return -1;
        const unsigned char value = reply_.data[offset_++];
        if (++consumedInChunk_ == reply_.chunkSize) {
            consumedInChunk_ = 0;
            nextChunkAt_ = millis() + reply_.intervalMs;
        }
        return value;
    }
    String readStringUntil(char delimiter) {
        std::string text;
        while (available()) {
            const int value = read();
            if (value == delimiter) break;
            text.push_back(char(value));
        }
        return String(text);
    }
    void println(const String& line = "") {
        const std::string request = line.c_str();
        requests.push_back(request);
        if (request.rfind("GET ", 0) == 0) {
            if (envoyReplies.empty()) throw std::logic_error("Unexpected Envoy request");
            load(envoyReplies.front());
            envoyReplies.pop_front();
        }
    }
    void println(const char* line) { println(String(line)); }
    void stop() { connected_ = false; reply_ = {}; offset_ = 0; }
    IPAddress remoteIP() const {
        unsigned int a, b, c, d;
        if (std::sscanf(host_.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
            throw std::logic_error("remoteIP called for cloud connection");
        return IPAddress(a, b, c, d);
    }
    uint16_t remotePort() const { return port_; }
    void load(Reply reply) {
        if (reply.chunkSize == 0) throw std::logic_error("Zero-sized network chunk");
        reply_ = std::move(reply);
        offset_ = consumedInChunk_ = 0;
        nextChunkAt_ = millis();
        connected_ = true;
    }
private:
    unsigned int generation_ = generation;
    Reply reply_;
    size_t offset_ = 0, consumedInChunk_ = 0;
    unsigned long nextChunkAt_ = 0;
    bool connected_ = false;
    std::string host_;
    uint16_t port_ = 0;
    void synchronize() {
        if (generation_ != generation) {
            stop();
            generation_ = generation;
        }
    }
};
using NetworkClientSecure = NetworkClient;

} // namespace EnvoyFirmware
