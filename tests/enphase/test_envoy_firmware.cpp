#include "../harness/test_runner.h"
#include "../harness/mock_arduino.h"
#include "../../EnvoyEnergySample.h"
#include <fstream>
#include <cctype>
#include <map>
#include "scripted_network.h"

namespace EnvoyFirmware {

using std::min;
struct MDNSStub {
    bool success = true;
    IPAddress ip{192, 168, 1, 100};
    bool begin(const char*) { return success; }
    IPAddress queryHost(const char*, int) { return ip; }
} MDNS;
const char* hostname = "router-test";
bool RMSextIPauto = false;
uint32_t RMSextIP = 0xc0a80164;
String EnphaseUser = "test@example.invalid", EnphasePwd = "test-only";
String EnphaseSerial = "12345", TokenEnphase, EnvoySessionIdCookie;
uint32_t previousTimeRMSMin, previousTimeRMSMax, previousTimeRMSMoy, previousTimeRMS;
uint32_t LastRMS_Millis, PeriodeProgMillis;
NetworkClientSecure clientSecu;
#define FILE_READ "r"
#define FILE_WRITE "w"
std::map<std::string, std::string> flashFiles;
bool flashWriteFails = false;
struct File {
    std::string* data = nullptr;
    explicit operator bool() const { return data != nullptr; }
    void print(const String& text) { *data += text.c_str(); }
    String readString() { return String(data->c_str()); }
    void close() { data = nullptr; }
};
struct LittleFSStub {
    bool exists(const char* path) { return flashFiles.count(path) != 0; }
    File open(const char* path, const char* mode) {
        if (std::string(mode) == FILE_WRITE) {
            if (flashWriteFails) return {};
            flashFiles[path].clear();
        } else if (!exists(path)) {
            return {};
        }
        return {&flashFiles[path]};
    }
} LittleFS;
#ifndef ENVOY_LEGACY_READER
void Setup_Enphase(bool NewToken = false);
#endif
void EcritureEnROM() {}
String urlEncode(String value) { return value; }
void networkYield() { advance_simulated_millis(1); }
#define yield networkYield
float ValJson(String, String);
#ifdef ENVOY_LEGACY_READER
float ValJson(const char*, const char*);
#else
double ValJson(const char*, const char*);
#endif
String StringJson(String, String);
#include "envoy_stats.inc"
float PactProd, PactConso_M, Tension_M, Intensite_M, Frequence, PowerFactor_M;
float Tension_M1, Tension_M2, Tension_M3, Intensite_M1, Intensite_M2, Intensite_M3;
int PuissanceS_M_inst, PuissanceI_M_inst, PVAS_M_inst, PVAI_M_inst;
int PuissanceS_M, PuissanceI_M, PVAS_M, PVAI_M;
int PuissanceS_T_inst, PuissanceI_T_inst, PVAS_T_inst, PVAI_T_inst;
int PuissanceS_T, PuissanceI_T, PVAS_T, PVAI_T;
float Puissance_M_moy, PVA_M_moy, Puissance_T_moy, PVA_T_moy;
bool LissageLong = false, Pva_valide = false, EnergieActiveValide = false, PuissanceRecue = false;
int cptLEDyellow = 0;
long LastwhDlvdCum = 0, LastwhRcvdCum = 0;
double Energie_M_Soutiree = 0, Energie_M_Injectee = 0;
float PfloatMax(float watts) { return std::clamp(watts, -36000.0f, 36000.0f); }
#include "envoy_filter.inc"
#ifndef ENVOY_READER_SOURCE
#define ENVOY_READER_SOURCE "../../Source_EnphaseEnvoy.ino"
#endif
#include ENVOY_READER_SOURCE
#undef yield

EnvoyEnergySample observed;
bool observedValid = false, observerEnabled = true;
int notifications = 0;
bool enabled() { return observerEnabled; }
void received(EnvoyEnergySample sample, bool valid) {
    observed = sample;
    observedValid = valid;
    notifications++;
}

void reset() {
    generation++;
    envoyReplies.clear();
    cloudReplies.clear();
    connectionResults.clear();
    connections.clear();
    requests.clear();
    flashFiles.clear();
    flashWriteFails = false;
    g_stockMessages.clear();
    TokenEnphase = std::string(80, 'T');
    EnvoySessionIdCookie = "";
    EnphaseUser = "test@example.invalid";
    EnphasePwd = "test-only";
    EnphaseSerial = "12345";
    RMSextIPauto = false;
    RMSextIP = 0xc0a80164;
    MDNS.success = true;
    MDNS.ip = IPAddress(192, 168, 1, 100);
    PactProd = PactConso_M = Tension_M = Intensite_M = Frequence = PowerFactor_M = 0;
    Tension_M1 = Tension_M2 = Tension_M3 = Intensite_M1 = Intensite_M2 = Intensite_M3 = 0;
    PuissanceS_M_inst = PuissanceI_M_inst = PVAS_M_inst = PVAI_M_inst = 0;
    PuissanceS_M = PuissanceI_M = PVAS_M = PVAI_M = 0;
    Puissance_M_moy = PVA_M_moy = Puissance_T_moy = PVA_T_moy = 0;
    LastwhDlvdCum = LastwhRcvdCum = 0;
    Energie_M_Soutiree = Energie_M_Injectee = 0;
    LissageLong = Pva_valide = EnergieActiveValide = PuissanceRecue = false;
    EnvoyCompteSuccesLectureComplete = EnvoyCompteTentativesLectureComplete = 0;
    EnvoyCompteTentativesRequeteAuthBearer = EnvoyCompteTentativesConnexionRenouvelee = 0;
    EnvoyCompteErreurTimeout = EnvoyCompteErreurConnectionClosed = EnvoyCompteErreurEchecConnect = 0;
    observed = {};
    observedValid = false;
    observerEnabled = true;
    notifications = 0;
    static const EnvoyReadObserver::Hooks hooks{enabled, ParseEnvoyReadingHeader, ParseEnvoyPhasePower, received};
    EnvoyReadObserver::registerHooks(&hooks);
}

std::string fixture(const char* name) {
    std::ifstream file(std::string("tests/enphase/fixtures/") + name);
    if (!file) throw std::runtime_error("Missing Envoy fixture");
    return std::string(std::istreambuf_iterator<char>(file), {});
}

std::string ok(const char* fixtureName = "envoy_readings_monophase_surplus.json") {
    return "HTTP/1.1 200 OK\r\nSet-Cookie: sessionId=local-test; Path=/\r\n\r\n" + fixture(fixtureName);
}

void queueRenewal(bool success = true) {
    cloudReplies.push_back({"HTTP/1.0 200 OK\r\n\r\n{\"session_id\":\"test-session\"}\n", false});
    cloudReplies.push_back({std::string("HTTP/1.0 200 OK\r\n\r\n") +
        (success ? std::string(90, 'N') : "{}") + "\n", false});
}

TEST_CASE(EnvoyCompatibility, AdditionalMeterDoesNotRejectGridReading) {
    reset();
    auto body = fixture("envoy_readings_monophase_surplus.json");
    body.insert(body.find_last_of(']'),
                ",{\"eid\":704643584,\"timestamp\":1718000000,\"voltage\":232.5,"
                "\"activePower\":700,\"channels\":[{\"activePower\":700},{},{}]}");
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n" + body});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_EQ(700, PactConso_M);
    ASSERT_EQ(1800, PuissanceI_M);
}

TEST_CASE(EnvoyCompatibility, RealEightMeterResponseIsAccepted) {
    reset();
    const auto body = fixture("envoy_readings_real_8meters.json");
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\n\r\n" + body});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_EQ(4022, (int)PactProd);
}

TEST_CASE(EnvoyCompatibility, ConsecutiveRealResponsesOnKeepAliveSocket) {
    reset();
    const auto body = fixture("envoy_readings_real_8meters.json");
    const auto reply = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + body;
    for (int i = 0; i < 3; ++i) {
        envoyReplies.push_back({reply, true, 5, 1460});
        LectureEnphase();
        ASSERT_EQ(unsigned(i + 1), EnvoyCompteSuccesLectureComplete);
        ASSERT_EQ(4022, (int)PactProd);
    }
}

#ifndef ENVOY_COMPATIBILITY_ONLY
TEST_CASE(EnvoyFirmware, RealMonophaseParsingAndObserver) {
    reset();
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_EQ(2500, PactProd);
    ASSERT_EQ(700, PactConso_M);
    ASSERT_EQ(1800, PuissanceI_M);
    ASSERT_EQ(0, PuissanceS_M);
    ASSERT_NEAR(232.5, Tension_M, 0.001);
    ASSERT_EQ(50, Frequence);
    ASSERT_NEAR(0.98, PowerFactor_M, 0.001);
    ASSERT_TRUE(observedValid);
    ASSERT_EQ(-1800, observed.powerW);
    ASSERT_EQ(uint64_t(1718000000), observed.stamp);
}

TEST_CASE(EnvoyFirmware, RealTriphaseAndImportSelection) {
    reset();
    envoyReplies.push_back({ok("envoy_readings_triphase_surplus.json")});
    LectureEnphase();
    ASSERT_EQ(6000, PactProd);
    ASSERT_EQ(2400, PactConso_M);
    ASSERT_EQ(3600, PuissanceI_M);
    ASSERT_NEAR(401.5, Tension_M, 0.001);
    ASSERT_NEAR(232.0, Tension_M2, 0.001);
    ASSERT_TRUE(observedValid);
    ASSERT_EQ(-3600, observed.powerW);
    envoyReplies.push_back({ok("envoy_readings_monophase_import.json")});
    LectureEnphase();
    ASSERT_EQ(0, PuissanceI_M);
    ASSERT_EQ(1650, PuissanceS_M);
    ASSERT_EQ(1650, observed.powerW);
}

TEST_CASE(EnvoyFirmware, TLSReuseCookieAndChangedHost) {
    reset();
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(size_t(1), connections.size());
    ASSERT_TRUE(requests[0].find("Authorization: Bearer ") != std::string::npos);
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(size_t(1), connections.size());
    ASSERT_TRUE(requests[1].find("Cookie: sessionId=local-test") != std::string::npos);
    RMSextIP = 0xc0a80165;
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(size_t(2), connections.size());
    ASSERT_EQ("192.168.1.101", connections.back().host);
    ASSERT_TRUE(requests.back().find("Host: 192.168.1.101") != std::string::npos);
}

TEST_CASE(EnvoyFirmware, Cookie401RetriesBearerWithoutCloudRenewal) {
    reset();
    EnvoySessionIdCookie = "expired";
    envoyReplies.push_back({"HTTP/1.1 401 Unauthorized\r\n\r\n"});
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(size_t(2), requests.size());
    ASSERT_TRUE(requests[1].find("Authorization: Bearer ") != std::string::npos);
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_TRUE(cloudReplies.empty());
}

TEST_CASE(EnvoyFirmware, Bearer401And403RenewalRateLimited) {
    for (int status : {401, 403}) {
        advance_simulated_millis(TOKEN_RETRY_MS + 100);
        reset();
        queueRenewal();
        envoyReplies.push_back({"HTTP/1.1 " + std::to_string(status) + " Rejected\r\n\r\n"});
        LectureEnphase();
        ASSERT_EQ(std::string(90, 'N'), std::string(TokenEnphase.c_str()));
        ASSERT_EQ(size_t(3), connections.size());
        ASSERT_FALSE(observedValid);
        envoyReplies.push_back({"HTTP/1.1 401 Rejected\r\n\r\n"});
        LectureEnphase();
        ASSERT_EQ(size_t(4), connections.size());
    }
}

TEST_CASE(EnvoyFirmware, MissingTokenAndFailedRenewalKeepPreviousToken) {
    advance_simulated_millis(TOKEN_RETRY_MS + 100);
    reset();
    TokenEnphase = "";
    queueRenewal();
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_EQ(std::string(90, 'N'), std::string(TokenEnphase.c_str()));
    advance_simulated_millis(TOKEN_RETRY_MS + 100);
    reset();
    const auto previous = std::string(TokenEnphase.c_str());
    queueRenewal(false);
    envoyReplies.push_back({"HTTP/1.1 403 Forbidden\r\n\r\n"});
    LectureEnphase();
    ASSERT_EQ(previous, std::string(TokenEnphase.c_str()));
    ASSERT_FALSE(observedValid);
}

TEST_CASE(EnvoyFirmware, StreamingBoundsTimeoutDisconnectAndChunks) {
    reset();
    char output[5];
    size_t length = 99;
    NetworkClient stream;
    stream.load({"abcd\n", false});
    ASSERT_EQ(READ_OK, ReadBufferUntilChar(stream, output, sizeof(output), length, '\n', 10));
    ASSERT_EQ(size_t(4), length);
    ASSERT_EQ("abcd", std::string(output));
    stream.load({"abcde\n", false});
    ASSERT_EQ(READ_TOO_LONG, ReadBufferUntilChar(stream, output, sizeof(output), length, '\n', 10));
    stream.load({"ab", false});
    ASSERT_EQ(READ_DISCONNECTED, ReadBufferUntilChar(stream, output, sizeof(output), length, '\n', 10));
    stream.load({"", true});
    const auto start = millis();
    ASSERT_EQ(READ_TIMEOUT, ReadBufferUntilChar(stream, output, sizeof(output), length, '\n', 10));
    ASSERT_EQ(10u, millis() - start);
    stream.load({"abcd\n", true, 3, 1});
    ASSERT_EQ(READ_OK, ReadBufferUntilChar(stream, output, sizeof(output), length, '\n', 10));
    ASSERT_EQ(READ_INVALID_ARGUMENT, ReadBufferUntilChar(stream, output, 0, length, '\n', 10));
}

TEST_CASE(EnvoyFirmware, FragmentedResponseAndMalformedBody) {
    reset();
    envoyReplies.push_back({ok(), true, 1, 13});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_TRUE(observedValid);
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n[", false});
    LectureEnphase();
    ASSERT_FALSE(observedValid);
    ASSERT_TRUE(EnvoyCompteErreurConnectionClosed > 0);
}

TEST_CASE(EnvoyFirmware, EnergyIndexesAndActualRCFilter) {
    reset();
    LissageLong = true;
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_NEAR(-540, Puissance_M_moy, 0.01);
    ASSERT_EQ(540, PuissanceI_M);
    ASSERT_EQ(0, Energie_M_Injectee);
    std::string response = ok();
    size_t index = 0;
    while ((index = response.find("320000.0", index)) != std::string::npos) {
        response.replace(index, 8, "320010.0");
        index += 8;
    }
    envoyReplies.push_back({response});
    LectureEnphase();
    ASSERT_EQ(10, Energie_M_Injectee);
    ASSERT_NEAR(-918, Puissance_M_moy, 0.01);
    ASSERT_EQ(-1800, observed.powerW);
}

TEST_CASE(EnvoyFirmware, DisabledObserverStillReadsAndErrorsInvalidate) {
    reset();
    observerEnabled = false;
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(0, notifications);
    ASSERT_EQ(1800, PuissanceI_M);
    observerEnabled = true;
    connectionResults.push_back(false);
    generation++;
    LectureEnphase();
    ASSERT_EQ(1, notifications);
    ASSERT_FALSE(observedValid);
}

TEST_CASE(EnvoyFirmware, Cookie403FallsBackToBearer) {
    reset();
    EnvoySessionIdCookie = "expired";
    envoyReplies.push_back({"HTTP/1.1 403 Forbidden\r\n\r\n"});
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_TRUE(requests[1].find("Authorization: Bearer ") != std::string::npos);
}

TEST_CASE(EnvoyFirmware, ExactStatusAndCompleteHeadersRequired) {
    for (const auto& response : {
        "HTTP/1.1 500 Error 200\r\n\r\n" + fixture("envoy_readings_monophase_surplus.json"),
        "HTTP/1.1 200 OK" + std::string(260, 'X') + "\r\n\r\n" + fixture("envoy_readings_monophase_surplus.json")
    }) {
        reset();
        envoyReplies.push_back({response, false});
        // Status retry loop may make two further attempts.
        envoyReplies.push_back({"", false});
        envoyReplies.push_back({"", false});
        LectureEnphase();
        ASSERT_EQ(0u, EnvoyCompteSuccesLectureComplete);
        ASSERT_FALSE(observedValid);
    }
}

TEST_CASE(EnvoyFirmware, MinimalAndCorruptJSONDoNotBecomeGoodReadings) {
    for (const auto& body : {
        "[{\"voltage\":230,\"channels\":[{},{},{}]},{\"timestamp\":1718000000,\"channels\":[{},{},{}]}]",
        "[{\"voltage\":230,\"channels\":[},},}]},{\"timestamp\":1718000000,\"channels\":[},},}]}]",
        "[{\"voltage\":230,\"activePower\":2500,\"channels\":[{\"activePower\":2500},{},{}]},"
        "{\"timestamp\":1718000000,\"channels\":[{\"activePower\":NaN},{},{}]}]"
    }) {
        reset();
        envoyReplies.push_back({std::string("HTTP/1.1 200 OK\r\n\r\n") + body, false});
        LectureEnphase();
        ASSERT_EQ(0u, EnvoyCompteSuccesLectureComplete);
        ASSERT_FALSE(observedValid);
    }
}

TEST_CASE(EnvoyFirmware, MinifiedFixturePreservesLastNumericDigit) {
    reset();
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, fixture("envoy_readings_monophase_surplus.json")));
    doc[1]["channels"][0]["actEnergyRcvd"] = 320019;
    std::string body;
    serializeJson(doc, body);
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n" + body});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_EQ(1800, PuissanceI_M);
    ASSERT_NEAR(7.82, Intensite_M1, 0.001);
    ASSERT_EQ(320019, LastwhRcvdCum);
}

TEST_CASE(EnvoyFirmware, WatchdogRunsOnAllEarlyReturnsAndRecovers) {
    for (int failure = 0; failure < 5; ++failure) {
        reset();
        envoyReplies.push_back({ok()});
        LectureEnphase();
        const auto successes = EnvoyCompteSuccesLectureComplete;
        advance_simulated_millis(9990);
        if (failure == 0) { generation++; connectionResults.push_back(false); }
        if (failure == 1) envoyReplies.push_back({"HTTP/1.1 500 Error\r\n\r\n"});
        if (failure == 2) { TokenEnphase = ""; EnphaseUser = ""; }
        if (failure == 3) envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n[", false});
        if (failure == 4) envoyReplies.push_back({"HTTP/1.1 200 OK\r\n", false});
        LectureEnphase();
        advance_simulated_millis(20);
        // Ensure every path is exercised after the exact watchdog threshold.
        if (failure == 0) connectionResults.push_back(false);
        if (failure == 1) envoyReplies.push_back({"HTTP/1.1 500 Error\r\n\r\n"});
        if (failure == 3) envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n[", false});
        if (failure == 4) envoyReplies.push_back({"HTTP/1.1 200 OK\r\n", false});
        LectureEnphase();
        ASSERT_EQ(successes, EnvoyCompteSuccesLectureComplete);
        ASSERT_EQ(0, PuissanceI_M_inst);
        ASSERT_EQ(0, PuissanceI_M);
        ASSERT_EQ(0, Puissance_M_moy);
        ASSERT_EQ(0, PVAI_M);
        ASSERT_EQ(0, PactProd);
        ASSERT_FALSE(EnergieActiveValide);
        ASSERT_FALSE(Pva_valide);
        ASSERT_FALSE(observedValid);
        TokenEnphase = std::string(80, 'T');
        EnphaseUser = "test@example.invalid";
        envoyReplies.push_back({ok()});
        LectureEnphase();
        ASSERT_EQ(1800, PuissanceI_M);
        ASSERT_TRUE(observedValid);
    }
}

TEST_CASE(EnvoyFirmware, ValidTokenIsNotRenewedPeriodically) {
    reset();
    EnvoySessionIdCookie = "old-cookie";
    advance_simulated_millis(45UL * 24 * 60 * 60 * 1000);
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_TRUE(cloudReplies.empty());
    ASSERT_EQ(size_t(1), connections.size());
    ASSERT_EQ(std::string(80, 'T'), std::string(TokenEnphase.c_str()));
}

TEST_CASE(EnvoyFirmware, CloudTimeoutHTTPErrorAndConnectFailureAreBounded) {
    for (int failure = 0; failure < 4; ++failure) {
        reset();
        const auto previous = std::string(TokenEnphase.c_str());
        if (failure == 0) cloudReplies.push_back({"", true});
        if (failure == 1) cloudReplies.push_back({"HTTP/1.0 500 Error\r\n\r\n" + std::string(90, 'X'), false});
        if (failure == 2) connectionResults.push_back(false);
        if (failure == 3) cloudReplies.push_back({"HTTP/1.0 200 OK\r\n\r\n{\"unexpected\":\"field\"}", false});
        const auto start = millis();
        RenouvelleTokenEnphase();
        ASSERT_TRUE(millis() - start <= 5000);
        ASSERT_EQ(previous, std::string(TokenEnphase.c_str()));
        ASSERT_EQ(size_t(1), connections.size());
    }
}

TEST_CASE(EnvoyFirmware, TruncatedClosingArrayIsNotASuccess) {
    reset();
    auto response = ok();
    response.resize(response.find_last_of(']'));
    envoyReplies.push_back({response, false});
    LectureEnphase();
    ASSERT_FALSE(observedValid);
    ASSERT_EQ(0u, EnvoyCompteSuccesLectureComplete);
}

TEST_CASE(EnvoyFirmware, LongHeaderLinesAreSkippedAndCookieStillCaptured) {
    reset();
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\nContent-Security-Policy: " + std::string(600, 'X') +
                            "\r\nSet-Cookie: sessionId=local-test; Path=/\r\n\r\n" +
                            fixture("envoy_readings_monophase_surplus.json")});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_EQ("local-test", std::string(EnvoySessionIdCookie.c_str()));

    reset();
    // Cookie tronqué par le tampon : jamais mis en cache.
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\nSet-Cookie: sessionId=" + std::string(400, 'S') +
                            "\r\n\r\n" + fixture("envoy_readings_monophase_surplus.json")});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_TRUE(EnvoySessionIdCookie.length() == 0);

    reset();
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\nX-Flood: " + std::string(20000, 'X') +
                            "\r\n\r\n" + fixture("envoy_readings_monophase_surplus.json")});
    LectureEnphase();
    ASSERT_EQ(0u, EnvoyCompteSuccesLectureComplete);
}

// En-têtes réels d'enlighten/entrez (anonymisés), corps et jetons factices de taille réelle.
TEST_CASE(EnvoyFirmware, RealisticCloudResponsesProvideToken) {
    reset();
    TokenEnphase = "";
    cloudReplies.push_back({fixture("cloud_login_success.http"), false, 2, 536});
    cloudReplies.push_back({fixture("cloud_token_success.http"), false, 2, 536});
    Setup_Enphase();
    ASSERT_EQ(size_t(2), connections.size());
    ASSERT_EQ(size_t(410), size_t(TokenEnphase.length()));
    bool sessionForwarded = false;
    for (const auto& line : requests)
        sessionForwarded |= line.find("\"session_id\":\"00000000000000000000000000000000\"") != std::string::npos;
    ASSERT_TRUE(sessionForwarded);
}

#ifndef ENVOY_LEGACY_READER
TEST_CASE(EnvoyFirmware, StoredTokenSkipsCloudAtBoot) {
    reset();
    TokenEnphase = "";
    const std::string stored(410, 'S');
    flashFiles[ENPHASE_TOKEN_FILE] = "{\"token\":\"" + stored + "\"}";
    Setup_Enphase();
    ASSERT_TRUE(connections.empty());
    ASSERT_EQ(stored, std::string(TokenEnphase.c_str()));
}

TEST_CASE(EnvoyFirmware, InvalidStoredTokenFallsBackToCloudAndIsReplaced) {
    for (const std::string& content : {std::string("{\"token\":\"") + std::string(TOKEN_MIN_LENGTH, 'S') + "\"}",
                                       std::string("{\"token\":"), std::string("{}")}) {
        reset();
        TokenEnphase = "";
        flashFiles[ENPHASE_TOKEN_FILE] = content;
        cloudReplies.push_back({fixture("cloud_login_success.http"), false, 2, 536});
        cloudReplies.push_back({fixture("cloud_token_success.http"), false, 2, 536});
        Setup_Enphase();
        ASSERT_EQ(size_t(2), connections.size());
        ASSERT_EQ(size_t(410), size_t(TokenEnphase.length()));
        ASSERT_EQ("{\"token\":\"" + std::string(TokenEnphase.c_str()) + "\"}", flashFiles[ENPHASE_TOKEN_FILE]);
        TokenEnphase = "";
        Setup_Enphase();
        ASSERT_EQ(size_t(2), connections.size());
        ASSERT_EQ(size_t(410), size_t(TokenEnphase.length()));
    }
}

TEST_CASE(EnvoyFirmware, RejectedStoredTokenIsRenewedAndOverwritten) {
    reset();
    const std::string stored(410, 'S');
    flashFiles[ENPHASE_TOKEN_FILE] = "{\"token\":\"" + stored + "\"}";
    TokenEnphase = stored;
    advance_simulated_millis(TOKEN_RETRY_MS);
    envoyReplies.push_back({"HTTP/1.1 401 Unauthorized\r\n\r\n"});
    queueRenewal();
    LectureEnphase();
    ASSERT_EQ(std::string(90, 'N'), std::string(TokenEnphase.c_str()));
    ASSERT_EQ("{\"token\":\"" + std::string(90, 'N') + "\"}", flashFiles[ENPHASE_TOKEN_FILE]);
}

TEST_CASE(EnvoyFirmware, FailedRenewalKeepsStoredTokenAndSaveFailureIsReported) {
    reset();
    const std::string stored(410, 'S');
    flashFiles[ENPHASE_TOKEN_FILE] = "{\"token\":\"" + stored + "\"}";
    TokenEnphase = stored;
    advance_simulated_millis(TOKEN_RETRY_MS);
    envoyReplies.push_back({"HTTP/1.1 401 Unauthorized\r\n\r\n"});
    queueRenewal(false);
    LectureEnphase();
    ASSERT_EQ(stored, std::string(TokenEnphase.c_str()));
    ASSERT_EQ("{\"token\":\"" + stored + "\"}", flashFiles[ENPHASE_TOKEN_FILE]);

    reset();
    TokenEnphase = "";
    flashWriteFails = true;
    queueRenewal();
    Setup_Enphase(true);
    ASSERT_EQ(std::string(90, 'N'), std::string(TokenEnphase.c_str()));
    ASSERT_TRUE(flashFiles.empty());
    bool reported = false;
    for (const auto& message : g_stockMessages)
        reported |= std::string(message.c_str()) == "Enphase token save failed";
    ASSERT_TRUE(reported);
}

TEST_CASE(EnvoyFirmware, CloudConnectFailureStopsBeforeTokenRequest) {
    reset();
    TokenEnphase = "";
    connectionResults.push_back(false);
    Setup_Enphase(true);
    ASSERT_EQ(size_t(1), connections.size());
    ASSERT_TRUE(TokenEnphase.length() == 0);
    ASSERT_TRUE(flashFiles.empty());
}
#endif

TEST_CASE(EnvoyFirmware, CaseInsensitiveHeaderNullBufferAndLongRequestFallback) {
    reset();
    auto response = ok();
    response.replace(response.find("Set-Cookie:"), 11, "set-cookie:");
    TokenEnphase = std::string(1400, 'T');
    envoyReplies.push_back({response});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_TRUE(requests[0].find(std::string(1400, 'T')) != std::string::npos);
    ASSERT_EQ("local-test", std::string(EnvoySessionIdCookie.c_str()));
    NetworkClient client;
    size_t length = 99;
    ASSERT_EQ(READ_INVALID_ARGUMENT, ReadBufferUntilChar(client, nullptr, 5, length, '\n', 10));
    ASSERT_EQ(size_t(0), length);
}

TEST_CASE(EnvoyFirmware, CounterRollbackAndRetryAfterClosedConnections) {
    reset();
    envoyReplies.push_back({"", false});
    envoyReplies.push_back({"", false});
    envoyReplies.push_back({ok()});
    LectureEnphase();
    ASSERT_EQ(size_t(3), connections.size());
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    envoyReplies.push_back({ok("envoy_readings_monophase_import.json")});
    LectureEnphase();
    ASSERT_EQ(0, Energie_M_Injectee);
    ASSERT_EQ(500, Energie_M_Soutiree);
}

TEST_CASE(EnvoyFirmware, OneHourActualReaderTracksDynamicSolarAndAppliances) {
    reset();
    LissageLong = true;
    JsonDocument document;
    ASSERT_FALSE(deserializeJson(document, fixture("envoy_readings_monophase_surplus.json")));
    double expectedFiltered = 0;
    long importIndex = 450000, exportIndex = 320000;
    for (int seconds = 0; seconds < 3600; seconds += 2) {
        const int solar = int(3000 * std::max(0.0, std::sin(seconds * 3.141592653589793 / 3600)));
        const int demand = 600 + (seconds >= 900 && seconds < 1500 ? 2000 : 0);
        const int grid = demand - solar;
        const bool outage = seconds >= 1800 && seconds < 1816;
        if (outage) {
            envoyReplies.push_back({"HTTP/1.1 503 Offline\r\n\r\n"});
        } else {
            document[0]["channels"][0]["activePower"] = solar;
            auto net = document[1]["channels"][0];
            net["activePower"] = grid;
            net["apparentPower"] = std::abs(grid);
            net["actEnergyDlvd"] = ++importIndex;
            net["actEnergyRcvd"] = ++exportIndex;
            document[1]["timestamp"] = uint64_t(1718000000 + seconds);
            std::string body;
            serializeJson(document, body);
            envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n" + body});
        }
        LectureEnphase();
        if (outage) {
            ASSERT_FALSE(observedValid);
            if (seconds >= 1810) {
                ASSERT_EQ(0, PuissanceI_M);
                ASSERT_EQ(0, PuissanceS_M);
                expectedFiltered = 0;
            }
        } else {
            expectedFiltered = 0.3f * grid + 0.7f * expectedFiltered;
            ASSERT_TRUE(observedValid);
            ASSERT_EQ(grid, observed.powerW);
            ASSERT_EQ(solar, PactProd);
            ASSERT_EQ(demand, PactConso_M);
            ASSERT_NEAR(expectedFiltered, Puissance_M_moy, 0.01);
            ASSERT_EQ(int(expectedFiltered), PuissanceS_M - PuissanceI_M);
        }
        advance_simulated_millis(2000);
    }
    ASSERT_EQ(1792u, EnvoyCompteSuccesLectureComplete);
    ASSERT_EQ(1791, Energie_M_Injectee);
    ASSERT_EQ(1791, Energie_M_Soutiree);
}

TEST_CASE(EnvoyFirmware, ThirtyTwoBitClockWrapRetryAndWatchdogThreshold) {
    const auto savedTime = millis();
    set_simulated_millis(UINT32_MAX - 2000UL);
    reset();
    ASSERT_TRUE(EnphaseTokenRetryAllowed());
    envoyReplies.push_back({ok()});
    LectureEnphase();
    TokenEnphase = "";
    EnphaseUser = "";
    set_simulated_millis(100);
    ASSERT_FALSE(EnphaseTokenRetryAllowed());
    LectureEnphase();
    ASSERT_EQ(1800, PuissanceI_M);
    set_simulated_millis(9000);
    LectureEnphase();
    ASSERT_EQ(0, PuissanceI_M);
    set_simulated_millis(TOKEN_RETRY_MS);
    ASSERT_TRUE(EnphaseTokenRetryAllowed());
    set_simulated_millis(savedTime);
}

TEST_CASE(EnvoyFirmware, MDNSResolutionFailureAndCloudFragmentation) {
    reset();
    RMSextIPauto = true;
    MDNS.success = false;
    Setup_Enphase();
    ASSERT_TRUE(connections.empty());
    MDNS.success = true;
    MDNS.ip = IPAddress(0, 0, 0, 0);
    Setup_Enphase();
    ASSERT_TRUE(connections.empty());
    MDNS.ip = IPAddress(192, 168, 1, 101);
    queueRenewal();
    for (auto& reply : cloudReplies) {
        reply.chunkSize = 7;
        reply.intervalMs = 2;
    }
    Setup_Enphase();
    ASSERT_EQ(uint32_t(0xc0a80165), RMSextIP);
    ASSERT_EQ(std::string(90, 'N'), std::string(TokenEnphase.c_str()));
    ASSERT_EQ(1000u, PeriodeProgMillis);
}

TEST_CASE(EnvoyFirmware, EmptyMeasurementsAreValidAndNoCredentialsNeverContactCloud) {
    reset();
    JsonDocument document;
    ASSERT_FALSE(deserializeJson(document, fixture("envoy_readings_monophase_surplus.json")));
    document[0]["channels"][0]["activePower"] = 0;
    document[1]["channels"][0]["activePower"] = 0;
    document[1]["channels"][0]["apparentPower"] = 0;
    std::string body;
    serializeJson(document, body);
    envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n" + body});
    LectureEnphase();
    ASSERT_EQ(1u, EnvoyCompteSuccesLectureComplete);
    ASSERT_TRUE(observedValid);
    ASSERT_EQ(0, PuissanceS_M);
    ASSERT_EQ(0, PuissanceI_M);
    reset();
    TokenEnphase = "";
    EnphaseUser = "";
    LectureEnphase();
    ASSERT_TRUE(connections.empty());
    ASSERT_FALSE(observedValid);
}

TEST_CASE(EnvoyFirmware, LargeEnergyIndexesKeepSingleWhResolution) {
    reset();
    JsonDocument document;
    ASSERT_FALSE(deserializeJson(document, fixture("envoy_readings_monophase_surplus.json")));
    for (int index : {2000000000, 2000000001, 1999999990, 1999999991}) {
        document[1]["channels"][0]["actEnergyDlvd"] = index;
        document[1]["channels"][0]["actEnergyRcvd"] = index;
        std::string body;
        serializeJson(document, body);
        envoyReplies.push_back({"HTTP/1.1 200 OK\r\n\r\n" + body});
        LectureEnphase();
        ASSERT_EQ(index, LastwhDlvdCum);
        ASSERT_EQ(index, LastwhRcvdCum);
    }
    ASSERT_EQ(2, Energie_M_Injectee);
    ASSERT_EQ(2, Energie_M_Soutiree);
}

String GS = "|", RS = ";", Source_data = "Enphase";
std::string ajaxFields() {
    String S;
#include "envoy_ajax.inc"
    return S.c_str();
}

TEST_CASE(EnvoyWeb, AjaxStatisticsShapeRatiosAndCredentialMasking) {
    reset();
    envoyReplies.push_back({ok()});
    LectureEnphase();
    const auto fields = ajaxFields();
    ASSERT_EQ(size_t(26), std::count(fields.begin(), fields.end(), ';'));
    ASSERT_TRUE(fields.find("Ok Received from Enphase;Ok Received from Envoy") != std::string::npos);
    ASSERT_TRUE(fields.find(TokenEnphase.c_str()) == std::string::npos);
    ASSERT_TRUE(fields.find(EnvoySessionIdCookie.c_str()) == std::string::npos);
    ASSERT_TRUE(fields.find("AVG: ") != std::string::npos);
    EnvoyCompteTentativesLectureComplete = 4;
    EnvoyCompteErreurConnectionClosed = 1;
    EnvoyCompteErreurTimeout = 2;
    EnvoyCompteTentativesConnexionRenouvelee = 3;
    const auto ratios = ajaxFields();
    ASSERT_TRUE(ratios.find(";25.00;50.00;75.00") != std::string::npos);
    EnvoyCompteTentativesLectureComplete = 0;
    ASSERT_TRUE(ajaxFields().find(";NA;NA;NA") != std::string::npos);
    EnphaseUser = "";
    ASSERT_TRUE(ajaxFields().find("Not Requested;Not Requested") != std::string::npos);
}

byte arrIP[4];
#include "envoy_common.inc"
TEST_CASE(EnvoyWeb, IPAndCSSConversionsPreserveProtocol) {
    ASSERT_EQ("192.168.1.100", std::string(IP2String(0xc0a80164).c_str()));
    ASSERT_EQ(byte(192), arrIP[3]);
    ASSERT_EQ(byte(100), arrIP[0]);
    ASSERT_EQ("#000001", std::string(ULtoHex(1).c_str()));
    ASSERT_EQ("#abcdef", std::string(ULtoHex(0xffabcdef).c_str()));
}
#endif

} // namespace EnvoyFirmware
