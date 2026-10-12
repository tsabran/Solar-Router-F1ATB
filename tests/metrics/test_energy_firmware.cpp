#include "../../EnergyMetrics.h"
#include "../../EnvoyEnergySample.h"
#include "../harness/test_runner.h"
#include <ctime>
#include <iostream>
#include <string>

namespace MetricsFirmware {
using String = std::string;
using portMUX_TYPE = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0;
void portENTER_CRITICAL(portMUX_TYPE*) {}
void portEXIT_CRITICAL(portMUX_TYPE*) {}
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
void StockMessage(String) {}

constexpr int MODE_SEQUENCEUR = 6;
constexpr int MODE_DEMISINUS = 5;
struct TestAction {
  bool SuiviEnergie = false;
  int Actif = 0;
  int IdxSequenceur = -1;
  int PuissanceCharge = 0;
  int tOnOff = 0;
  int temperatureChannel = -1;
  int CanalTempEnCours(int) const { return temperatureChannel; }
};
TestAction LesActions[10];
int NbActions = 5;
int Gpio[10] = {};
int Retard[10] = {};
bool EnergieAutonome[10] = {};
int HeureCouranteDeci = 1200;
int TemperatureValide[4] = {};
int testTrame = 0;
double PmaxReseau = 36000;
String Source = "Enphase";
energy_metrics::Counter CompteursPotentiels;

time_t testEpoch = 1700000000;
time_t observerTime(time_t*) { return testEpoch; }
#define time observerTime
#include "../../EnergiePotentielle.ino"
#undef time

static void configure() {
  InvaliderCommandeEnergie();
  CompteursPotentiels = {};
  clockMs = 0;
  testEpoch = 1700000000;
  NbActions = 5;
  PmaxReseau = 36000;
  std::fill_n(TemperatureValide, 4, 0);
  InitialiserObservationEnergie();
  Source = "Enphase";
  testTrame = 0;
  for (int i = 0; i < 10; ++i) {
    LesActions[i] = {};
    EnergieAutonome[i] = false;
    Gpio[i] = 0;
    Retard[i] = 100;
  }
  LesActions[1].Actif = MODE_SEQUENCEUR;
  LesActions[1].SuiviEnergie = true;
  EnergieAutonome[1] = true;
  for (int i = 2; i <= 4; ++i) {
    LesActions[i].Actif = MODE_DEMISINUS;
    LesActions[i].IdxSequenceur = 1;
    LesActions[i].PuissanceCharge = 1000;
    Gpio[i] = 20 + i;
  }
  ActualiserCommandeEnergie();
}

static void sample(double power) {
  const String json = "{\"timestamp\":" + std::to_string(testEpoch) +
    ",\"activePower\":" + std::to_string(power) + "}";
  EnvoyEnergySample reading;
  const bool valid = ParseEnvoyReadingSample(json.c_str(), reading);
  RecevoirEchantillonEnergie(reading, valid);
  ActualiserEnergiePotentielle();
}

TEST_CASE(MetricsFirmware, Eligibility) {
  configure();
  ASSERT_TRUE(ConfigurationEnergieValide && CommandeEnergieValide);
  ASSERT_TRUE(CommandeEnergie.commandedW == 0 && CommandeEnergie.headroomW == 3000);
  for (int i = 0; i < NbActions; ++i) {
    for (int force : {-10, 10}) {
      LesActions[i].tOnOff = force;
      ActualiserCommandeEnergie();
      ASSERT_TRUE(!CommandeEnergieValide);
      LesActions[i].tOnOff = 0;
    }
  }
  EnergieAutonome[1] = false;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!CommandeEnergieValide);
  EnergieAutonome[1] = true;
  LesActions[1].temperatureChannel = 0;
  TemperatureValide[0] = 0;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!CommandeEnergieValide);
  TemperatureValide[0] = 5;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(CommandeEnergieValide);
  testTrame = 99;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!CommandeEnergieValide);
}

TEST_CASE(MetricsFirmware, InvalidConfiguration) {
  configure();
  LesActions[2].PuissanceCharge = 0;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!ConfigurationEnergieValide && !CommandeEnergieValide);
  LesActions[2].PuissanceCharge = 1000;
  for (int mode : {0, 1, 2, 3, 4, 6}) {
    LesActions[2].Actif = mode;
    ActualiserCommandeEnergie();
    ASSERT_TRUE(!ConfigurationEnergieValide && !CommandeEnergieValide);
  }
  LesActions[2].Actif = MODE_DEMISINUS;
  Gpio[2] = -1;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!ConfigurationEnergieValide);
  configure();
  LesActions[0].SuiviEnergie = true;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!ConfigurationEnergieValide && !CommandeEnergieValide);
  configure();
  Source = "UxI";
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!CommandeEnergieValide);
  configure();
  LesActions[1].SuiviEnergie = false;
  ActualiserCommandeEnergie();
  ASSERT_TRUE(!ConfigurationEnergieValide && !SuiviMesuresEnergie);
}

TEST_CASE(MetricsFirmware, IntegrationAndNoRegulationChanges) {
  configure();
  sample(-600);
  for (clockMs = 200; clockMs <= 2000; clockMs += 200) {
    ActualiserEnergiePotentielle();
    ActualiserCommandeEnergie();
  }
  ASSERT_TRUE(std::abs(CompteursPotentiels.exportWh - 1.0 / 3.0) < 1e-9);
  ASSERT_TRUE(CompteursPotentiels.importWh == 0);
  for (int i = 2; i <= 4; ++i) {
    ASSERT_TRUE(Retard[i] == 100 && Gpio[i] == 20 + i);
    ASSERT_TRUE(LesActions[i].Actif == MODE_DEMISINUS);
    ASSERT_TRUE(LesActions[i].PuissanceCharge == 1000);
  }
  LesActions[2].tOnOff = 30;
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(!CommandeEnergieValide);
  const double old = CompteursPotentiels.exportWh;
  clockMs += 200;
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(CompteursPotentiels.exportWh == old);
  clockMs += 5000;
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(!CompteursPotentiels.fresh(clockMs));
  ASSERT_TRUE(CompteursPotentiels.exportWh == old);
}

TEST_CASE(MetricsFirmware, IntraSecondChangesAndSourceAge) {
  configure();
  Retard[2] = Retard[3] = Retard[4] = 50;
  ActualiserCommandeEnergie();
  const uint64_t stamp = uint64_t(testEpoch);
  RecevoirEchantillonEnergie({stamp, 600}, true);
  ActualiserEnergiePotentielle();
  clockMs = 500;
  RecevoirEchantillonEnergie({stamp, -900}, true);
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(std::abs(CompteursPotentiels.importWh - 600 * 0.5 / 3600) < 1e-9);
  clockMs = 1000;
  RecevoirEchantillonEnergie({stamp, -900}, true);
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(std::abs(CompteursPotentiels.exportWh - 900 * 0.5 / 3600) < 1e-9);
  clockMs = 1200;
  RecevoirEchantillonEnergie({stamp - 10, 900}, true);
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(!CompteursPotentiels.fresh(clockMs));
  const double oldImport = CompteursPotentiels.importWh;
  const double oldExport = CompteursPotentiels.exportWh;
  clockMs = 1400;
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(CompteursPotentiels.importWh == oldImport);
  ASSERT_TRUE(CompteursPotentiels.exportWh == oldExport);
}

TEST_CASE(MetricsFirmware, SourceExpiresWithoutNewResponse) {
  configure();
  const uint64_t stamp = uint64_t(testEpoch);
  RecevoirEchantillonEnergie({stamp, -600}, true);
  ActualiserEnergiePotentielle();
  clockMs = 500;
  ActualiserEnergiePotentielle();
  const double oldExport = CompteursPotentiels.exportWh;
  testEpoch += 4;
  clockMs = 600;
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(!CompteursPotentiels.fresh(clockMs));
  ASSERT_TRUE(CompteursPotentiels.exportWh == oldExport);
}

TEST_CASE(MetricsFirmware, ReaderObservationLifecycle) {
  configure();
  const String header = "},{\"timestamp\":" + std::to_string(testEpoch) +
    ",\"activePower\":-300,\"channels\":[";
  {
    EnvoyReadObserver observer;
    observer.header(header);
    observer.complete();
  }
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(CompteursPotentiels.gridW == -300);
  clockMs = 500;
  {
    EnvoyReadObserver observer;
    observer.header(header.c_str());
    char phase[] = "{\"activePower\":600}";
    observer.phase(phase);
    observer.complete();
  }
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(CompteursPotentiels.gridW == 600);
  ASSERT_TRUE(std::abs(CompteursPotentiels.exportWh - 300 * 0.5 / 3600) < 1e-9);
  {
    EnvoyReadObserver observer;
    observer.header(header);
    // A return before complete() must invalidate the previous reading.
  }
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(!CompteursPotentiels.fresh(clockMs));
  {
    EnvoyReadObserver observer;
    observer.header(header);
    observer.phase("{\"voltage\":230}");
    observer.complete();
  }
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(!CompteursPotentiels.fresh(clockMs));
  EnvoyReadObserver::registerHooks(nullptr);
  const uint32_t generation = GenerationMesureEnergie;
  {
    EnvoyReadObserver observer;
    observer.header(header);
    observer.complete();
  }
  ASSERT_TRUE(GenerationMesureEnergie == generation);
}

TEST_CASE(MetricsFirmware, MultipleEmptyAndInvalidGroups) {
  configure();
  LesActions[4].SuiviEnergie = true;
  ActualiserCommandeEnergie();
  ASSERT_FALSE(ConfigurationEnergieValide);
  ASSERT_EQ("multiple_groups", std::string(EtatEnergie));
  configure();
  for (int j = 2; j <= 4; j++) LesActions[j].IdxSequenceur = -1;
  ActualiserCommandeEnergie();
  ASSERT_FALSE(ConfigurationEnergieValide);
  ASSERT_EQ("missing_nominal_or_unsupported_output", std::string(EtatEnergie));
  for (int mode : {0, 1, 2, 3, 4, 5}) {
    configure();
    LesActions[1].Actif = mode;
    ActualiserCommandeEnergie();
    ASSERT_FALSE(ConfigurationEnergieValide);
    ASSERT_EQ("invalid_group", std::string(EtatEnergie));
  }
}

TEST_CASE(MetricsFirmware, InvalidControlDelayAndTemperatureChannel) {
  for (int delay : {-1, 101}) {
    configure();
    Retard[2] = delay;
    ActualiserCommandeEnergie();
    ASSERT_FALSE(ConfigurationEnergieValide);
  }
  for (int channel : {4, 1000}) {
    configure();
    LesActions[1].temperatureChannel = channel;
    ActualiserCommandeEnergie();
    ASSERT_TRUE(ConfigurationEnergieValide);
    ASSERT_FALSE(CommandeEnergieValide);
    ASSERT_EQ("invalid_temperature", std::string(EtatEnergie));
  }
}

TEST_CASE(MetricsFirmware, ReconfigurationInvalidatesWithoutLosingCounters) {
  configure();
  sample(-900);
  clockMs = 1000;
  ActualiserEnergiePotentielle();
  const double total = CompteursPotentiels.exportWh;
  ASSERT_NEAR(0.25, total, 1e-9);
  InvaliderCommandeEnergie();
  ASSERT_FALSE(LectureEnergieActive());
  ASSERT_FALSE(CompteursPotentiels.fresh(clockMs));
  const auto generation = GenerationMesureEnergie;
  RecevoirEchantillonEnergie({uint64_t(testEpoch), -900}, true);
  ASSERT_EQ(generation, GenerationMesureEnergie);
  clockMs = 2000;
  ActualiserCommandeEnergie();
  sample(-900);
  clockMs = 3000;
  ActualiserEnergiePotentielle();
  ASSERT_NEAR(total + 0.25, CompteursPotentiels.exportWh, 1e-9);
}

TEST_CASE(MetricsFirmware, InvalidAndRepeatedDatedReportsDoNotExtendSourceLife) {
  configure();
  sample(-900);
  clockMs = 1000;
  ActualiserEnergiePotentielle();
  const double total = CompteursPotentiels.exportWh;
  RecevoirMesureEnergie("{invalid");
  ActualiserEnergiePotentielle();
  ASSERT_FALSE(CompteursPotentiels.fresh(clockMs));
  ASSERT_EQ(total, CompteursPotentiels.exportWh);
  sample(-900);
  testEpoch += 4;
  clockMs = 1500;
  RecevoirEchantillonEnergie({uint64_t(testEpoch - 4), -900}, true);
  ActualiserEnergiePotentielle();
  ASSERT_FALSE(CompteursPotentiels.fresh(clockMs));
  ASSERT_EQ(total, CompteursPotentiels.exportWh);
}

TEST_CASE(MetricsFirmware, UnavailableAndForcedCountersStayAvailableButFrozen) {
  configure();
  Retard[2] = Retard[3] = Retard[4] = 50;
  ActualiserCommandeEnergie();
  sample(900);
  clockMs = 1000;
  ActualiserEnergiePotentielle();
  ASSERT_NEAR(0.25, CompteursPotentiels.importWh, 1e-9);
  EnergieAutonome[1] = false;
  ActualiserCommandeEnergie();
  clockMs = 1500;
  ActualiserEnergiePotentielle();
  ASSERT_TRUE(CompteursPotentiels.fresh(clockMs));
  ASSERT_TRUE(ConfigurationEnergieValide);
  ASSERT_FALSE(CommandeEnergieValide);
  ASSERT_EQ(900, CompteursPotentiels.gridW);
  ASSERT_EQ(0, CompteursPotentiels.importW);
  ASSERT_NEAR(0.25, CompteursPotentiels.importWh, 1e-9);
}

TEST_CASE(MetricsFirmware, DisabledObserverDoesNotParseOrNotify) {
  configure();
  LesActions[1].SuiviEnergie = false;
  ActualiserCommandeEnergie();
  const auto generation = GenerationMesureEnergie;
  {
    EnvoyReadObserver observer;
    observer.header("not JSON");
    observer.phase("not JSON");
    observer.complete();
  }
  ASSERT_EQ(generation, GenerationMesureEnergie);
}

} // namespace MetricsFirmware
