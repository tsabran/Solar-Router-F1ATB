#include "../harness/test_runner.h"
#include "../harness/mock_arduino.h"
#include "../../EnergyMetrics.h"
#include <ArduinoJson.h>

namespace ArduinoJson {
template <> struct Converter<String> {
    static void toJson(const String& value, JsonVariant destination) {
        destination.set(value.c_str());
    }
};
}

namespace MetricsMQTT {

struct Publication { std::string topic; std::string payload; };
struct MQTTClient {
    std::vector<Publication> publications;
    bool publishSuccess = true;
    bool publish(const char* topic, const char* payload) {
        ASSERT_TRUE(5 + 2 + std::strlen(topic) + std::strlen(payload) <= 1700);
        publications.push_back({topic, payload});
        return publishSuccess;
    }
} clientMQTT;

energy_metrics::Counter CompteursPotentiels;
energy_metrics::Control CommandeEnergie;
bool ConfigurationEnergieValide = false, CommandeEnergieValide = false;
String Source = "Enphase", MQTTdeviceName = "Router_Test", SSR = "sensor";
const char* StateTopic = "router/test/state";
const char* AvailableTopic = "router/test/availability";
const char* PrefixMQTT = "homeassistant/";
const char* DEVICE = R"({"identifiers":["Router_Test"],"name":"Router Test"})";
const char* EtatEnergie = "disabled";
int GroupeEnergie = -1, MQTTRepet = 30;
std::vector<std::string> messages;
void RecordMessage(String message) { messages.emplace_back(message.c_str()); }
int updateCalls = 0;
void ActualiserEnergiePotentielle() {
    updateCalls++;
    CompteursPotentiels.advance(millis());
}
#define StockMessage RecordMessage
#include "metrics_mqtt.inc"
#undef StockMessage

void reset() {
    clientMQTT = {};
    CompteursPotentiels = {};
    CommandeEnergie = {};
    ConfigurationEnergieValide = CommandeEnergieValide = false;
    Source = "Enphase";
    DEVICE = R"({"identifiers":["Router_Test"],"name":"Router Test"})";
    EtatEnergie = "disabled";
    GroupeEnergie = -1;
    messages.clear();
    updateCalls = 0;
    set_simulated_millis(0);
}

JsonDocument state() {
    PublierEnergiePotentielle();
    ASSERT_EQ(std::string("router/test/state/regulation"), clientMQTT.publications.back().topic);
    JsonDocument result;
    ASSERT_FALSE(deserializeJson(result, clientMQTT.publications.back().payload));
    return result;
}

TEST_CASE(MetricsMQTT, DiscoveryContractAndAvailability) {
    reset();
    DiscoveryEnergiePotentielle();
    ASSERT_EQ(size_t(2), clientMQTT.publications.size());
    for (int i = 0; i < 2; i++) {
        const std::string key = i == 0 ? "PotentialImportWh" : "PotentialExportWh";
        const auto& published = clientMQTT.publications[i];
        ASSERT_EQ("homeassistant/sensor/Router_Test_" + key + "/config", published.topic);
        JsonDocument discovery;
        ASSERT_FALSE(deserializeJson(discovery, published.payload));
        ASSERT_EQ("energy", std::string(discovery["device_class"].as<const char*>()));
        ASSERT_EQ("Wh", std::string(discovery["unit_of_meas"].as<const char*>()));
        ASSERT_EQ("total_increasing", std::string(discovery["state_class"].as<const char*>()));
        ASSERT_EQ(3, discovery["suggested_display_precision"].as<int>());
        ASSERT_EQ(70, discovery["expire_after"].as<int>());
        ASSERT_EQ("all", std::string(discovery["availability_mode"].as<const char*>()));
        ASSERT_EQ(size_t(2), discovery["availability"].size());
        ASSERT_EQ(AvailableTopic, std::string(discovery["availability"][0]["topic"].as<const char*>()));
        ASSERT_EQ("router/test/state/regulation", std::string(discovery["availability"][1]["topic"].as<const char*>()));
        ASSERT_EQ("{{ 'online' if value_json.Available else 'offline' }}",
                  std::string(discovery["availability"][1]["value_template"].as<const char*>()));
        ASSERT_EQ("{{ value_json." + key + " }}", std::string(discovery["val_tpl"].as<const char*>()));
        ASSERT_EQ("Router_Test", std::string(discovery["device"]["identifiers"][0].as<const char*>()));
    }
}

TEST_CASE(MetricsMQTT, StatePrecisionTypesAndAvailability) {
    reset();
    CompteursPotentiels.importWh = 0.123456789;
    CompteursPotentiels.exportWh = 12.345678901;
    CommandeEnergie = {true, 1500, 1500};
    CompteursPotentiels.sample(0, 1700000000, -900);
    CompteursPotentiels.setControl(0, CommandeEnergie);
    ConfigurationEnergieValide = CommandeEnergieValide = true;
    GroupeEnergie = 1;
    EtatEnergie = "autonomous";
    JsonDocument payload = state();
    const auto& json = clientMQTT.publications.back().payload;
    ASSERT_TRUE(json.find("\"PotentialImportWh\":0.123457,") != std::string::npos);
    ASSERT_TRUE(json.find("\"PotentialExportWh\":12.345679,") != std::string::npos);
    ASSERT_TRUE(payload["PotentialImportWh"].is<double>());
    ASSERT_NEAR(0.123457, payload["PotentialImportWh"].as<double>(), 5e-7);
    ASSERT_NEAR(12.345679, payload["PotentialExportWh"].as<double>(), 5e-7);
    ASSERT_TRUE(payload["Available"].as<bool>());
    ASSERT_TRUE(payload["Eligible"].as<bool>());
    ASSERT_EQ(-900, payload["GridW"].as<double>());
    ASSERT_EQ(900, payload["PotentialExportW"].as<double>());
    ASSERT_EQ(1500, payload["CommandedW"].as<double>());
    ASSERT_EQ(1500, payload["HeadroomW"].as<double>());
    ASSERT_EQ("source_power_sample_hold", std::string(payload["Integration"].as<const char*>()));
    ASSERT_EQ(1, payload["Group"].as<int>());
    ASSERT_EQ(0, payload["UptimeMs"].as<int>());
    ASSERT_EQ(1, updateCalls);
    CommandeEnergieValide = false;
    CompteursPotentiels.setControl(0, {});
    EtatEnergie = "forced";
    payload = state();
    ASSERT_TRUE(payload["Available"].as<bool>());
    ASSERT_FALSE(payload["Eligible"].as<bool>());
    ASSERT_EQ(0, payload["PotentialExportW"].as<double>());
    set_simulated_millis(3100);
    payload = state();
    ASSERT_FALSE(payload["Available"].as<bool>());
    ASSERT_EQ(0, payload["GridW"].as<double>());
    ASSERT_NEAR(12.345679, payload["PotentialExportWh"].as<double>(), 5e-7);
}

TEST_CASE(MetricsMQTT, DisabledUnsupportedAndPublicationErrors) {
    reset();
    ASSERT_FALSE(state()["Available"].as<bool>());
    ConfigurationEnergieValide = CommandeEnergieValide = true;
    CompteursPotentiels.sample(0, 1700000000, 900);
    Source = "UxI";
    auto payload = state();
    ASSERT_FALSE(payload["Available"].as<bool>());
    ASSERT_FALSE(payload["Eligible"].as<bool>());
    clientMQTT.publishSuccess = false;
    PublierEnergiePotentielle();
    ASSERT_EQ("Erreur publication energie potentielle", messages.back());
    DiscoveryEnergiePotentielle();
    ASSERT_EQ("Erreur publication discovery energie", messages.back());
    DEVICE = "{invalid";
    const auto previous = clientMQTT.publications.size();
    DiscoveryEnergiePotentielle();
    ASSERT_EQ(previous, clientMQTT.publications.size());
    ASSERT_EQ("Erreur discovery energie : device JSON invalide", messages.back());
}

} // namespace MetricsMQTT
