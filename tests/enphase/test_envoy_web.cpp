#include "../harness/test_runner.h"
#include "../harness/mock_arduino.h"

#define STATIC_ASSET_TAG "test-assets"
#include "../../StaticAssetCache.h"

namespace EnvoyWeb {

struct Server {
    std::string body, type, cacheControl, encoding, vary;
    String version;
    String acceptedEncoding = "gzip, deflate, br";
    bool hasEncodingHeader = true;
    int statusCode = 0;
    unsigned int cacheHeaderWrites = 0;
    size_t contentLength = 0;
    unsigned int chunks = 0;
    bool terminated = false;
    String arg(const char*) { return version; }
    bool hasHeader(const char*) { return hasEncodingHeader; }
    String header(const char*) { return acceptedEncoding; }
    String uri() { return "/test"; }
    void sendHeader(const char* name, const char* value) {
        if (std::string(name) == "Cache-Control") {
            cacheControl = value;
            cacheHeaderWrites++;
        }
        if (std::string(name) == "Content-Encoding") encoding = value;
        if (std::string(name) == "Vary") vary = value;
    }
    void setContentLength(size_t length) { contentLength = length; }
    void send(int status, const char* mime, const String& value) {
        statusCode = status;
        type = mime;
        body = value.c_str();
    }
    void send_P(int status, const char* mime, const char* value) { send(status, mime, value); }
    void send_P(int status, const char* mime, const char* value, size_t length) {
        statusCode = status;
        type = mime;
        contentLength = length;
        body.assign(value, length);
    }
    void sendContent(const String& value) {
        body += value.c_str();
        chunks++;
        terminated = value.length() == 0;
    }
    void sendContent(const char* value, size_t length) {
        body.append(value, length);
        chunks++;
    }
} server;
constexpr size_t CONTENT_LENGTH_UNKNOWN = size_t(-1);
unsigned long esp_get_free_internal_heap_size() { return 100000; }
void CacheStaticResource();
void SendStaticAsset(const char*, const char*);
void SendStaticAssetPayload(const char*, const char*, const StaticAssetPayload&);
std::vector<std::string> logs;
void RecordWebLog(const String& message) { logs.emplace_back(message.c_str()); }
#define HttpLog(msg) ((void)0)
const char* MainJS1 = "main-js-1", *MainJS2 = "main-js-2", *MainJS3 = "main-js-3";
const char* MainHtml = "main-html";
const char* PageBrute = "brute-html", *PageBruteJS1 = "brute-js-1", *PageBruteJS2 = "brute-js-2";
const char* CommunCSS = "css-body{}";
String Couleurs, ES = "~";
int IdxStock2s = 299;
int tabPw_Maison_2s[300], tabPva_Maison_2s[300], tabPw_Triac_2s[300], tabPva_Triac_2s[300];
int tab_histo_2s_ouverture[3][300];
String nomSondeFixe, Source_data, EnphaseSerial = "0", GS = "|", RS = ";";
int PuisMaxS_M = 1, PuisMaxI_M = 2, PuisMaxS_T = 3, PuisMaxI_T = 4;
int IdxStockPW = 599, NbActions = 3, ITmode = 0;
int tabPw_Maison_5mn[600], tabPw_Triac_5mn[600];
int tabTemperature_5mn[4][600], tab_histo_ouverture[3][600];
float temperature[4] = {1, 2, 3, 4};
struct Action { int Actif; String Titre; };
Action LesActions[3] = {{1, "Triac"}, {1, "Relais"}, {0, "Disabled"}};
#include "envoy_append.inc"
#define TelnetPrintln RecordWebLog
#include "envoy_web.inc"
#undef TelnetPrintln

TEST_CASE(EnvoyWeb, StaticBodiesAndRuntimeProbeSelection) {
    for (const auto& source : {"Enphase", "UxIx2", "ShellyEm", "ShellyPro"}) {
        for (const auto& channel : {"0", "3"}) {
            server = {};
            nomSondeFixe = "Sonde";
            Source_data = source;
            EnphaseSerial = channel;
            handleRuntimeJS();
            const bool dual = Source_data == "UxIx2" ||
                ((Source_data == "ShellyEm" || Source_data == "ShellyPro") && EnphaseSerial != "3");
            ASSERT_EQ(std::string(dual ? "var biSonde=true;\r\n" : "var biSonde=false;\r\n"),
                      server.body);
        }
    }
    const std::pair<void (*)(), const char*> pages[] = {
        {handleMainJS1, MainJS1}, {handleMainJS2, MainJS2},
        {handleMainJS3, MainJS3}, {handleBrute, PageBrute},
        {handleBruteJS1, PageBruteJS1}, {handleBruteJS2, PageBruteJS2}
    };
    for (const auto& [handler, expected] : pages) {
        server = {};
        handler();
        ASSERT_EQ(std::string(expected), server.body);
        ASSERT_EQ(server.body.size(), server.contentLength);
        ASSERT_TRUE(server.encoding.empty());
    }
}

TEST_CASE(EnvoyWeb, RuntimeCssPreservesColorsAndStaticCssIsSeparate) {
    for (const char* couleurs : {"", "aabbccddeeff112233"}) {
        server = {};
        Couleurs = couleurs;
        handleRuntimeCSS();
        const std::string expected = std::string(couleurs[0] ? "#aabbcc" : "#ffffff");
        ASSERT_EQ(std::string("text/css"), server.type);
        ASSERT_TRUE(server.body.find(expected) != std::string::npos);
        ASSERT_TRUE(server.body.find("css-body{}") == std::string::npos);
        server = {};
        handleCommunCSS();
        ASSERT_EQ(std::string("css-body{}"), server.body);
    }
}

TEST_CASE(EnvoyWeb, ImmutableOnlyForCurrentVersionAndStaticBodies) {
    for (const char* version : {"", "old-build", "test-assets"}) {
        server = {};
        server.version = version;
        for (auto handler : {handleMainJS1, handleMainJS2, handleMainJS3,
                             handleCommunCSS, handleBruteJS1, handleBruteJS2}) {
            server = {};
            server.version = version;
            handler();
            ASSERT_EQ(std::string(version) == "test-assets" ?
                      std::string("public, max-age=31536000, immutable") :
                      std::string("no-store"), server.cacheControl);
            ASSERT_EQ(1u, server.cacheHeaderWrites);
        }
        for (auto handler : {handleRoot, handleRuntimeJS, handleRuntimeCSS, handleBrute}) {
            server = {};
            server.version = version;
            handler();
            ASSERT_EQ(std::string("no-store"), server.cacheControl);
            ASSERT_EQ(1u, server.cacheHeaderWrites);
        }
    }
}

TEST_CASE(EnvoyWeb, GzipNegotiationRespectsQualityWildcardAndCase) {
    for (const char* value : {"gzip", "br, gzip, deflate", "GZip ; Q = 0.5", "*",
                             "br;q=1, *;q=0.01", "gzip;q=0.001", "gzip;q=1.000",
                             "*;q=0, gzip;q=1"}) {
        ASSERT_TRUE(StaticAssets::AcceptsGzip(value));
    }
    ASSERT_TRUE(StaticAssets::AcceptsGzip(nullptr));
    for (const char* value : {"", "identity", "xgzip", "gzip;q=0", "*;q=0",
                             "*;q=1,gzip;q=0", "gzip;q=0.000", "gzip;q=1.001",
                             "gzip;q=-1", "gzip;q=abc", "gzip;q=0.0001", "gzip;q=",
                             "gzip;q=0,gzip;q=1", "gzip;level=9", "gzip;q=1;q=0"}) {
        ASSERT_FALSE(StaticAssets::AcceptsGzip(value));
    }
}

TEST_CASE(EnvoyWeb, GzipUsesExplicitBinaryLengthAndVary) {
    const char binary[] = {'\x1f', '\x8b', '\0', '\x7f', '\0', '\x41'};
    for (bool hasHeader : {false, true}) {
        server = {};
        server.hasEncodingHeader = hasHeader;
        SendStaticAssetPayload("text/javascript", binary, {sizeof(binary), true});
        ASSERT_EQ(200, server.statusCode);
        ASSERT_EQ(std::string(binary, sizeof(binary)), server.body);
        ASSERT_EQ(sizeof(binary), server.contentLength);
        ASSERT_EQ(std::string("gzip"), server.encoding);
        ASSERT_EQ(std::string("Accept-Encoding"), server.vary);
        ASSERT_EQ(std::string("text/javascript"), server.type);
    }
}

TEST_CASE(EnvoyWeb, GzipRefusalIsVisibleAndNeverImmutable) {
    for (const char* accepted : {"", "identity", "gzip;q=0, *;q=1"}) {
        server = {};
        logs.clear();
        server.acceptedEncoding = accepted;
        server.version = "test-assets";
        SendStaticAssetPayload("text/html", "not sent", {8, true});
        ASSERT_EQ(406, server.statusCode);
        ASSERT_EQ(std::string("text/plain"), server.type);
        ASSERT_EQ(std::string("no-store"), server.cacheControl);
        ASSERT_EQ(1u, server.cacheHeaderWrites);
        ASSERT_EQ(std::string("Accept-Encoding"), server.vary);
        ASSERT_TRUE(server.encoding.empty());
        ASSERT_TRUE(server.body.find("not acceptable") != std::string::npos);
        ASSERT_EQ(size_t(1), logs.size());
    }
}

TEST_CASE(EnvoyWeb, MissingAssetMetadataProducesVisibleServerError) {
    server = {};
    logs.clear();
    SendStaticAsset("text/html", nullptr);
    ASSERT_EQ(500, server.statusCode);
    ASSERT_EQ(std::string("no-store"), server.cacheControl);
    ASSERT_EQ(size_t(1), logs.size());
}

TEST_CASE(EnvoyWeb, ChunkedHistoryOrderAllChannelsAndActionEligibility) {
    for (int i = 0; i < 600; i++) {
        tabPw_Maison_5mn[i] = i;
        tabPw_Triac_5mn[i] = -i;
        for (int c = 0; c < 4; c++) tabTemperature_5mn[c][i] = c * 1000 + i;
        for (int a = 0; a < 3; a++) tab_histo_ouverture[a][i] = a * 1000 + i;
    }
    for (int mains : {0, 1}) {
        ITmode = mains;
        server = {};
        handleAjaxHisto48h();
        String expected = "1;2;3;4|";
        for (int i = 0; i < 600; i++) expected += String((i + 599) % 600) + ",";
        expected += GS;
        for (int i = 0; i < 600; i++) expected += String(-((i + 599) % 600)) + ",";
        expected += GS;
        for (int c = 0; c < 4; c++) {
            for (int i = 0; i < 600; i++)
                expected += String(float(c * 1000 + (i + 599) % 600) * 0.1) + ",";
            expected += String(temperature[c]) + "|";
        }
        for (int a = mains ? 0 : 1; a < 2; a++) {
            expected += GS;
            for (int i = 0; i < 600; i++) expected += String(a * 1000 + (i + 599) % 600) + RS;
            expected += LesActions[a].Titre;
        }
        ASSERT_EQ(std::string(expected.c_str()), server.body);
        ASSERT_EQ(CONTENT_LENGTH_UNKNOWN, server.contentLength);
        ASSERT_TRUE(server.terminated);
        ASSERT_TRUE(server.chunks > 8);
        ASSERT_TRUE(server.body.find("Disabled") == std::string::npos);
    }
}

TEST_CASE(EnvoyWeb, Data10mnOrderAndActionEligibility) {
    for (int i = 0; i < 300; i++) {
        tabPw_Maison_2s[i] = i; tabPva_Maison_2s[i] = 2 * i;
        tabPw_Triac_2s[i] = -i; tabPva_Triac_2s[i] = -2 * i;
        for (int a = 0; a < 3; a++) tab_histo_2s_ouverture[a][i] = a * 100 - i;
    }
    Source_data = "Enphase";
    server = {};
    handleAjaxData10mn();
    String expected = Source_data + GS;
    for (int i = 0; i < 300; i++) {
        const int k = (i + 299) % 300;
        expected += String(k) + "," + String(2 * k) + ",";
    }
    expected += GS;
    for (int i = 0; i < 300; i++) {
        const int k = (i + 299) % 300;
        expected += String(-k) + "," + String(-2 * k) + ",";
    }
    for (int a = 0; a < 2; a++) {
        expected += GS + String(a) + ES;
        for (int i = 0; i < 300; i++) expected += String(a * 100 - (i + 299) % 300) + RS;
    }
    ASSERT_EQ(std::string(expected.c_str()), server.body);
    ASSERT_TRUE(server.terminated);
}

} // namespace EnvoyWeb
