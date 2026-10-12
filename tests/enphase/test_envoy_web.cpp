#include "../harness/test_runner.h"
#include "../harness/mock_arduino.h"

namespace EnvoyWeb {

struct Server {
    std::string body, type;
    size_t contentLength = 0;
    unsigned int chunks = 0;
    bool terminated = false;
    void setContentLength(size_t length) { contentLength = length; }
    void send(int status, const char* mime, const String& value) {
        if (status != 200) throw std::logic_error("Unexpected HTTP status");
        type = mime;
        body = value.c_str();
    }
    void send_P(int status, const char* mime, const char* value) { send(status, mime, value); }
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
void CacheEtClose(int) {}
const char* MainJS1 = "main-js-1", *MainJS2 = "main-js-2", *MainJS3 = "main-js-3";
const char* PageBrute = "brute-html", *PageBruteJS1 = "brute-js-1", *PageBruteJS2 = "brute-js-2";
String nomSondeFixe, Source_data, EnphaseSerial = "0", GS = "|", RS = ";";
int PuisMaxS_M = 1, PuisMaxI_M = 2, PuisMaxS_T = 3, PuisMaxI_T = 4;
int IdxStockPW = 599, NbActions = 3, ITmode = 0;
int tabPw_Maison_5mn[600], tabPw_Triac_5mn[600];
int tabTemperature_5mn[4][600], tab_histo_ouverture[3][600];
float temperature[4] = {1, 2, 3, 4};
struct Action { int Actif; String Titre; };
Action LesActions[3] = {{1, "Triac"}, {1, "Relais"}, {0, "Disabled"}};
#include "envoy_web.inc"

TEST_CASE(EnvoyWeb, StaticStreamingMatchesBodyAndContentLength) {
    for (const auto& source : {"Enphase", "UxIx2", "ShellyEm", "ShellyPro"}) {
        for (const auto& channel : {"0", "3"}) {
            server = {};
            nomSondeFixe = "Sonde";
            Source_data = source;
            EnphaseSerial = channel;
            handleMainJS1();
            const bool dual = Source_data == "UxIx2" ||
                ((Source_data == "ShellyEm" || Source_data == "ShellyPro") && EnphaseSerial != "3");
            ASSERT_EQ((dual ? "var biSonde=true;\r\n" : "var biSonde=false;\r\n") +
                      std::string(MainJS1), server.body);
            ASSERT_EQ(server.body.size(), server.contentLength);
        }
    }
    const std::pair<void (*)(), const char*> pages[] = {
        {handleMainJS2, MainJS2}, {handleMainJS3, MainJS3}, {handleBrute, PageBrute},
        {handleBruteJS1, PageBruteJS1}, {handleBruteJS2, PageBruteJS2}
    };
    for (const auto& [handler, expected] : pages) {
        server = {};
        handler();
        ASSERT_EQ(std::string(expected), server.body);
    }
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

} // namespace EnvoyWeb
