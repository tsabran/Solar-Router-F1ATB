#include "../EnergyMetrics.h"
#include "../EnvoyEnergySample.h"
#include <cassert>
#include <ctime>
#include <iostream>
#include <string>

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

time_t testEpoch = std::time(nullptr);
time_t observerTime(time_t*) { return testEpoch; }
#define time observerTime
#include "../EnergiePotentielle.ino"
#undef time

static void configure() {
  InvaliderCommandeEnergie();
  CompteursPotentiels = {};
  clockMs = 0;
  testEpoch = std::time(nullptr);
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
  const String json = "{\"timestamp\":" + std::to_string(time(nullptr)) +
    ",\"activePower\":" + std::to_string(power) + "}";
  EnvoyEnergySample reading;
  const bool valid = ParseEnvoyReadingSample(json.c_str(), reading);
  RecevoirEchantillonEnergie(reading, valid);
  ActualiserEnergiePotentielle();
}

static void eligibility() {
  configure();
  assert(ConfigurationEnergieValide && CommandeEnergieValide);
  assert(CommandeEnergie.commandedW == 0 && CommandeEnergie.headroomW == 3000);
  for (int i = 0; i < NbActions; ++i) {
    for (int force : {-10, 10}) {
      LesActions[i].tOnOff = force;
      ActualiserCommandeEnergie();
      assert(!CommandeEnergieValide);
      LesActions[i].tOnOff = 0;
    }
  }
  EnergieAutonome[1] = false;
  ActualiserCommandeEnergie();
  assert(!CommandeEnergieValide);
  EnergieAutonome[1] = true;
  LesActions[1].temperatureChannel = 0;
  TemperatureValide[0] = 0;
  ActualiserCommandeEnergie();
  assert(!CommandeEnergieValide);
  TemperatureValide[0] = 5;
  ActualiserCommandeEnergie();
  assert(CommandeEnergieValide);
  testTrame = 99;
  ActualiserCommandeEnergie();
  assert(!CommandeEnergieValide);
}

static void invalidConfiguration() {
  configure();
  LesActions[2].PuissanceCharge = 0;
  ActualiserCommandeEnergie();
  assert(!ConfigurationEnergieValide && !CommandeEnergieValide);
  LesActions[2].PuissanceCharge = 1000;
  for (int mode : {0, 1, 2, 3, 4, 6}) {
    LesActions[2].Actif = mode;
    ActualiserCommandeEnergie();
    assert(!ConfigurationEnergieValide && !CommandeEnergieValide);
  }
  LesActions[2].Actif = MODE_DEMISINUS;
  Gpio[2] = -1;
  ActualiserCommandeEnergie();
  assert(!ConfigurationEnergieValide);
  configure();
  LesActions[0].SuiviEnergie = true;
  ActualiserCommandeEnergie();
  assert(!ConfigurationEnergieValide && !CommandeEnergieValide);
  configure();
  Source = "UxI";
  ActualiserCommandeEnergie();
  assert(!CommandeEnergieValide);
  configure();
  LesActions[1].SuiviEnergie = false;
  ActualiserCommandeEnergie();
  assert(!ConfigurationEnergieValide && !SuiviMesuresEnergie);
}

static void integrationAndNoRegulationChanges() {
  configure();
  sample(-600);
  for (clockMs = 200; clockMs <= 2000; clockMs += 200) {
    ActualiserEnergiePotentielle();
    ActualiserCommandeEnergie();
  }
  assert(std::abs(CompteursPotentiels.exportWh - 1.0 / 3.0) < 1e-9);
  assert(CompteursPotentiels.importWh == 0);
  for (int i = 2; i <= 4; ++i) {
    assert(Retard[i] == 100 && Gpio[i] == 20 + i);
    assert(LesActions[i].Actif == MODE_DEMISINUS);
    assert(LesActions[i].PuissanceCharge == 1000);
  }
  LesActions[2].tOnOff = 30;
  ActualiserEnergiePotentielle();
  assert(!CommandeEnergieValide);
  const double old = CompteursPotentiels.exportWh;
  clockMs += 200;
  ActualiserEnergiePotentielle();
  assert(CompteursPotentiels.exportWh == old);
  clockMs += 5000;
  ActualiserEnergiePotentielle();
  assert(!CompteursPotentiels.fresh(clockMs));
  assert(CompteursPotentiels.exportWh == old);
}

static void intraSecondChangesAndSourceAge() {
  configure();
  Retard[2] = Retard[3] = Retard[4] = 50;
  ActualiserCommandeEnergie();
  const uint64_t stamp = uint64_t(time(nullptr));
  RecevoirEchantillonEnergie({stamp, 600}, true);
  ActualiserEnergiePotentielle();
  clockMs = 500;
  RecevoirEchantillonEnergie({stamp, -900}, true);
  ActualiserEnergiePotentielle();
  assert(std::abs(CompteursPotentiels.importWh - 600 * 0.5 / 3600) < 1e-9);
  clockMs = 1000;
  RecevoirEchantillonEnergie({stamp, -900}, true);
  ActualiserEnergiePotentielle();
  assert(std::abs(CompteursPotentiels.exportWh - 900 * 0.5 / 3600) < 1e-9);
  clockMs = 1200;
  RecevoirEchantillonEnergie({stamp - 10, 900}, true);
  ActualiserEnergiePotentielle();
  assert(!CompteursPotentiels.fresh(clockMs));
  const double oldImport = CompteursPotentiels.importWh;
  const double oldExport = CompteursPotentiels.exportWh;
  clockMs = 1400;
  ActualiserEnergiePotentielle();
  assert(CompteursPotentiels.importWh == oldImport);
  assert(CompteursPotentiels.exportWh == oldExport);
}

static void sourceExpiresWithoutNewResponse() {
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
  assert(!CompteursPotentiels.fresh(clockMs));
  assert(CompteursPotentiels.exportWh == oldExport);
}

static void readerObservationLifecycle() {
  configure();
  const String header = "},{\"timestamp\":" + std::to_string(testEpoch) +
    ",\"activePower\":-300,\"channels\":[";
  {
    EnvoyReadObserver observer;
    observer.header(header);
    observer.complete();
  }
  ActualiserEnergiePotentielle();
  assert(CompteursPotentiels.gridW == -300);
  clockMs = 500;
  {
    EnvoyReadObserver observer;
    observer.header(header.c_str());
    char phase[] = "{\"activePower\":600}";
    observer.phase(phase);
    observer.complete();
  }
  ActualiserEnergiePotentielle();
  assert(CompteursPotentiels.gridW == 600);
  assert(std::abs(CompteursPotentiels.exportWh - 300 * 0.5 / 3600) < 1e-9);
  {
    EnvoyReadObserver observer;
    observer.header(header);
    // A return before complete() must invalidate the previous reading.
  }
  ActualiserEnergiePotentielle();
  assert(!CompteursPotentiels.fresh(clockMs));
  {
    EnvoyReadObserver observer;
    observer.header(header);
    observer.phase("{\"voltage\":230}");
    observer.complete();
  }
  ActualiserEnergiePotentielle();
  assert(!CompteursPotentiels.fresh(clockMs));
  EnvoyReadObserver::registerHooks(nullptr);
  const uint32_t generation = GenerationMesureEnergie;
  {
    EnvoyReadObserver observer;
    observer.header(header);
    observer.complete();
  }
  assert(GenerationMesureEnergie == generation);
}

int main() {
  eligibility();
  invalidConfiguration();
  integrationAndNoRegulationChanges();
  intraSecondChangesAndSourceAge();
  sourceExpiresWithoutNewResponse();
  readerObservationLifecycle();
  std::cout << "Firmware energy observer: all tests passed\n";
}
