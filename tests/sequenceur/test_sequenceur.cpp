#include "../harness/test_runner.h"
#include "stubs/Arduino.h"
#include "stubs/WiFiClient.h"

namespace SequencerFirmware {

std::vector<std::string> messages;
void SequencerStockMessage(String message) { messages.emplace_back(message.c_str()); }
void SequencerTelnetPrintln(String message) { messages.emplace_back(message.c_str()); }

#define StockMessage SequencerStockMessage
#define TelnetPrintln SequencerTelnetPrintln
#include "../../Actions.cpp"

constexpr int Capacity = 10;
Action LesActions[Capacity];
int NbActions = 0, ReacCACSI = 1, HeureCouranteDeci = 1200, LTARFbin = 0;
int ModePara = 0, ITmode = 1, RetardVx = -1, testTrame = 0, testPulse = 0;
float PuissanceS_M = 0, PuissanceI_M = 0;
bool LissageLong = false, dispPw = false, dispAct = false;
float temperature[8] = {}, TemperatureValide[8] = {};
float RetardF[Capacity], IntegrErrorPw[Capacity], Propor[Capacity];
float DeriveF[Capacity], LastErrorPw[Capacity];
int Retard[Capacity], Actif[Capacity], PulseOn[Capacity], PulseTotal[Capacity];
int PulseComptage[Capacity], OutOn[Capacity], Gpio[Capacity];

#include "sequenceur_firmware.inc"
#undef StockMessage
#undef TelnetPrintln
static_assert(Capacity == LES_ACTIONS_LENGTH);

void reset(int count = 5) {
    NbActions = count;
    ReacCACSI = 1;
    HeureCouranteDeci = 1200;
    LTARFbin = ModePara = testTrame = testPulse = 0;
    ITmode = 1;
    RetardVx = -1;
    PuissanceS_M = PuissanceI_M = 0;
    LissageLong = dispPw = dispAct = false;
    messages.clear();
    SequencerIO::digital.clear();
    SequencerIO::pwm.clear();
    SequencerIO::modes.clear();
    SequencerIO::attachments.clear();
    SequencerIO::networkCalls = 0;
    set_simulated_millis(1000);
    for (int i = 0; i < Capacity; i++) {
        LesActions[i] = Action(i);
        LesActions[i].NbPeriode = 1;
        LesActions[i].Hfin[0] = 2400;
        LesActions[i].Type[0] = 2;
        LesActions[i].ONouvre[0] = 0;
        LesActions[i].Gpio = Gpio[i] = 10 + i;
        LesActions[i].OrdreOn = String(10 + i) + "|1";
        RetardF[i] = IntegrErrorPw[i] = Retard[i] = 100;
        Propor[i] = DeriveF[i] = LastErrorPw[i] = 0;
        Actif[i] = PulseOn[i] = PulseTotal[i] = PulseComptage[i] = 0;
        OutOn[i] = 1;
    }
    std::fill_n(TemperatureValide, 8, 0);
    std::fill_n(temperature, 8, -120);
    LesActions[1].Actif = MODE_SEQUENCEUR;
    for (int j = 2; j < count; j++) {
        LesActions[j].Actif = MODE_DEMISINUS;
        LesActions[j].IdxSequenceur = 1;
        LesActions[j].PuissanceCharge = 1000;
    }
}

void demand(int percent, int parent = 1) {
    LesActions[parent].ONouvre[0] = percent;
    GestionOverproduction();
}

float power(int parent = 1) {
    float watts = 0;
    for (int i = 1; i < NbActions; i++) {
        if (LesActions[i].IdxSequenceur == parent && LesActions[i].Actif != MODE_INACTIF) {
            const int nominal = LesActions[i].PuissanceCharge > 0 ? LesActions[i].PuissanceCharge : 1000;
            watts += nominal * (1 - RetardF[i] / 100);
        }
    }
    return watts;
}

void staircase(float total, int parent = 1) {
    int partial = 0;
    bool closedSeen = false;
    for (int i = 1; i < NbActions; i++) {
        if (LesActions[i].IdxSequenceur != parent || LesActions[i].Actif == MODE_INACTIF) continue;
        ASSERT_TRUE(RetardF[i] >= 0 && RetardF[i] <= 100);
        ASSERT_EQ(int(std::round(RetardF[i])), Retard[i]);
        if (RetardF[i] > 0.001f && RetardF[i] < 99.999f) partial++;
        if (closedSeen) ASSERT_NEAR(100, RetardF[i], 0.001);
        if (RetardF[i] > 0.001f) closedSeen = true;
    }
    ASSERT_TRUE(partial <= 1);
    ASSERT_NEAR(total * (1 - RetardF[parent] / 100), power(parent), 0.02);
}

} // namespace SequencerFirmware

using namespace SequencerFirmware;

TEST_CASE(Sequenceur, DistributionUniformeEtBornes) {
    reset();
    for (int opening = 0; opening <= 100; opening++) {
        demand(opening);
        ASSERT_NEAR(opening * 30, power(), 0.02);
        staircase(3000);
    }
    demand(50);
    ASSERT_NEAR(0, RetardF[2], 0.001);
    ASSERT_NEAR(50, RetardF[3], 0.001);
    ASSERT_NEAR(100, RetardF[4], 0.001);
}

TEST_CASE(Sequenceur, ChargesHeterogenesEtOrdreDesIndices) {
    reset();
    LesActions[2].PuissanceCharge = 500;
    LesActions[3].PuissanceCharge = 2000;
    LesActions[4].PuissanceCharge = 1000;
    for (int opening = 0; opening <= 100; opening++) {
        demand(opening);
        ASSERT_NEAR(opening * 35, power(), 0.02);
        staircase(3500);
    }
    demand(20);
    ASSERT_EQ(0, Retard[2]);
    ASSERT_EQ(90, Retard[3]);
    ASSERT_EQ(100, Retard[4]);
}

TEST_CASE(Sequenceur, PuissanceParDefautEtPrecisionISR) {
    reset(3);
    for (int nominal : {0, -100, 1, 5000}) {
        LesActions[2].PuissanceCharge = nominal;
        for (int opening = 0; opening <= 100; opening++) {
            demand(opening);
            ASSERT_NEAR(100 - opening, RetardF[2], 0.001);
            ASSERT_EQ(100 - opening, Retard[2]);
        }
    }
}

TEST_CASE(Sequenceur, AucunRelaisEtCapaciteMaximale) {
    reset(2);
    demand(70);
    ASSERT_EQ(30, Retard[1]);
    for (const auto& write : SequencerIO::digital) ASSERT_FALSE(write.pin == 11);
    reset(Capacity);
    for (int opening = 0; opening <= 100; opening++) {
        demand(opening);
        ASSERT_NEAR(opening * 80, power(), 0.05);
        staircase(8000);
    }
}

TEST_CASE(Sequenceur, GroupesIndependantsEtParentApresRelais) {
    reset(7);
    LesActions[5].Actif = MODE_SEQUENCEUR;
    LesActions[5].IdxSequenceur = -1;
    LesActions[2].IdxSequenceur = LesActions[6].IdxSequenceur = 5;
    LesActions[5].ONouvre[0] = 75;
    demand(25);
    ASSERT_NEAR(500, power(1), 0.01);
    ASSERT_NEAR(1500, power(5), 0.01);
    staircase(2000, 1);
    staircase(2000, 5);
}

TEST_CASE(Sequenceur, BypassPIDEtPlanificationDesEnfants) {
    reset();
    for (int j = 2; j < 5; j++) {
        LesActions[j].Type[0] = 1;
        LesActions[j].Ki = 255;
        LesActions[j].CanalTemp[0] = 0;
        LesActions[j].PID = true;
        IntegrErrorPw[j] = 23;
    }
    PuissanceS_M = 4000;
    demand(50);
    ASSERT_NEAR(1500, power(), 0.01);
    for (int j = 2; j < 5; j++) ASSERT_EQ(23, IntegrErrorPw[j]);
}

TEST_CASE(Sequenceur, ForcagesLocauxEtRetourAuPID) {
    reset();
    LesActions[2].tOnOff = -60;
    LesActions[4].tOnOff = 60;
    LesActions[4].ForceOuvre = 25;
    demand(50);
    ASSERT_EQ(100, Retard[2]);
    ASSERT_EQ(50, Retard[3]);
    ASSERT_EQ(75, Retard[4]);
    ASSERT_NEAR(75, RetardF[1], 0.001);
    ASSERT_EQ(75, Retard[1]);
    ASSERT_EQ(100, IntegrErrorPw[1]);
    LesActions[2].tOnOff = LesActions[4].tOnOff = 0;
    demand(50);
    ASSERT_NEAR(50, RetardF[1], 0.001);
    staircase(3000);
}

TEST_CASE(Sequenceur, ForcageParentEtLimitationOuverture) {
    reset();
    LesActions[1].tOnOff = 60;
    LesActions[1].ForceOuvre = 40;
    demand(100);
    ASSERT_NEAR(1200, power(), 0.01);
    LesActions[1].tOnOff = -60;
    demand(100);
    ASSERT_EQ(0, power());
    LesActions[4].tOnOff = 60;
    demand(100);
    ASSERT_NEAR(1000, power(), 0.01);
    ASSERT_NEAR(100.0f * 2 / 3, RetardF[1], 0.001);
}

TEST_CASE(Sequenceur, ConditionsDuParentEtTemperatureInvalide) {
    reset();
    LesActions[1].Hdeb[0] = 1300;
    demand(100);
    ASSERT_EQ(0, power());
    LesActions[1].Hdeb[0] = 0;
    LesActions[1].Tarif[0] = 2;
    LTARFbin = 1;
    demand(100);
    ASSERT_EQ(0, power());
    LTARFbin = 2;
    demand(100);
    ASSERT_EQ(3000, power());
    LesActions[1].CanalTemp[0] = 0;
    demand(100);
    ASSERT_EQ(0, power());
    TemperatureValide[0] = 1;
    temperature[0] = 40;
    LesActions[1].Tinf[0] = 600;
    LesActions[1].Tsup[0] = 300;
    demand(100);
    ASSERT_EQ(3000, power());
    temperature[0] = 70;
    demand(100);
    ASSERT_EQ(0, power());
}

TEST_CASE(Sequenceur, SequenceurSansSortiePhysiqueOuReseau) {
    reset();
    Action& parent = LesActions[1];
    parent.Host = "unexpected";
    parent.OrdreOn = "12|1";
    parent.OrdreOff = "/off";
    parent.InitGpio(1000);
    ASSERT_EQ(-1, parent.Gpio);
    parent.RelaisOn();
    ASSERT_TRUE(parent.On);
    parent.tOnOff = -60;
    parent.Prioritaire();
    ASSERT_TRUE(parent.On);
    parent.Arreter();
    ASSERT_FALSE(parent.On);
    demand(70);
    for (const auto& write : SequencerIO::digital) ASSERT_FALSE(write.pin == 11);
    ASSERT_TRUE(SequencerIO::pwm.empty());
    ASSERT_TRUE(SequencerIO::modes.empty());
    ASSERT_TRUE(SequencerIO::attachments.empty());
    ASSERT_EQ(0, SequencerIO::networkCalls);
}

TEST_CASE(Sequenceur, SortiesMultiTrainDemiSinusEtPWM) {
    reset(6);
    LesActions[2].Actif = MODE_MULTISINUS;
    LesActions[3].Actif = MODE_TRAINSINUS;
    LesActions[4].Actif = MODE_DEMISINUS;
    LesActions[5].Actif = MODE_PWM;
    PulseComptage[2] = 100;
    PulseTotal[4] = 7;
    demand(62);
    ASSERT_EQ(tabPulseSinusOn[100], PulseOn[2]);
    ASSERT_EQ(tabPulseSinusTotal[100], PulseTotal[2]);
    ASSERT_EQ(0, PulseComptage[2]);
    ASSERT_EQ(100, PulseOn[3]);
    ASSERT_EQ(99, PulseTotal[3]);
    ASSERT_EQ(48, PulseOn[4]);
    ASSERT_EQ(0, PulseTotal[4]);
    ASSERT_EQ(0, SequencerIO::pwm.back().value);
    demand(90);
    ASSERT_NEAR(153, SequencerIO::pwm.back().value, 1);
    OutOn[5] = 0;
    demand(90);
    ASSERT_NEAR(102, SequencerIO::pwm.back().value, 1);
    testTrame = 17;
    testPulse = 6;
    demand(90);
    ASSERT_EQ(6, PulseOn[3]);
    ASSERT_EQ(17, PulseTotal[3]);
    demand(0);
    for (int i = 2; i < 6; i++) ASSERT_EQ(0, PulseOn[i]);
    ASSERT_EQ(0, SequencerIO::pwm.back().value);
}

TEST_CASE(Sequenceur, NonRegressionActionsAutonomesEtTriac) {
    reset();
    LesActions[0].Actif = MODE_DECOUPE_ONOFF;
    LesActions[0].ONouvre[0] = 42;
    LesActions[4].IdxSequenceur = -1;
    LesActions[4].Actif = MODE_DECOUPE_ONOFF;
    LesActions[4].ONouvre[0] = 100;
    demand(50);
    ASSERT_EQ(58, Retard[0]);
    ASSERT_EQ(0, Retard[4]);
    ASSERT_TRUE(LesActions[4].On);
    ASSERT_NEAR(1000, power(), 0.01);
    ASSERT_TRUE(LissageLong);
    LesActions[4].Type[0] = 1;
    demand(50);
    ASSERT_FALSE(LesActions[4].On);
    LesActions[0].Type[0] = 4;
    LesActions[0].Vmax[0] = 100;
    ITmode = -1;
    demand(50);
    ASSERT_EQ(100, Retard[0]);
}

TEST_CASE(Sequenceur, BoucleFermeeVariationsSolairesEtChargeMaison) {
    reset();
    LesActions[1].Type[0] = 4;
    LesActions[1].Vmin[0] = -20;
    LesActions[1].Vmax[0] = 100;
    LesActions[1].Ki = 20;
    for (float surplus : {0.0f, 300.0f, 1500.0f, 2800.0f, 100.0f, -400.0f, 1800.0f, 4000.0f}) {
        for (int tick = 0; tick < 300; tick++) {
            if (tick % 10 == 0) {
                const float grid = power() - surplus;
                PuissanceS_M = std::max(grid, 0.0f);
                PuissanceI_M = std::max(-grid, 0.0f);
            }
            advance_simulated_millis(200);
            GestionOverproduction();
            staircase(3000);
        }
        ASSERT_NEAR(std::clamp(surplus - 20, 0.0f, 3000.0f), power(), 1);
    }
    LesActions[1].tOnOff = -60;
    GestionOverproduction();
    ASSERT_EQ(0, power());
    for (int j = 2; j < 5; j++) ASSERT_EQ(0, PulseOn[j]);
}

TEST_CASE(Sequenceur, ParentSupprimeDesactiveOuInvalideCoupeLesRelais) {
    for (int parent : {-2, 0, 2, 5, 500}) {
        reset();
        demand(100);
        LesActions[2].IdxSequenceur = parent;
        GestionOverproduction();
        ASSERT_EQ(100, Retard[2]);
        ASSERT_EQ(0, PulseOn[2]);
    }
    reset();
    demand(100);
    LesActions[1].Actif = MODE_INACTIF;
    GestionOverproduction();
    for (int j = 2; j < 5; j++) {
        ASSERT_EQ(100, Retard[j]);
        ASSERT_EQ(0, PulseOn[j]);
    }
}

TEST_CASE(Sequenceur, RelaisInactifExcluEtInterdictionImbrication) {
    reset();
    LesActions[2].Actif = MODE_INACTIF;
    demand(50);
    ASSERT_EQ(100, Retard[2]);
    ASSERT_NEAR(1000, power(), 0.01);
    ASSERT_EQ(0, Retard[3]);
    ASSERT_EQ(100, Retard[4]);
    reset();
    LesActions[2].Actif = MODE_SEQUENCEUR;
    demand(100);
    ASSERT_EQ(100, Retard[2]);
    ASSERT_EQ(0, Retard[1]);
}

TEST_CASE(Sequenceur, ForcagesHorsBornesNeDebordentPasLesTablesISR) {
    reset();
    LesActions[2].Actif = MODE_MULTISINUS;
    LesActions[2].tOnOff = 60;
    LesActions[2].ForceOuvre = 255;
    demand(40);
    ASSERT_TRUE(Retard[2] >= 0 && Retard[2] <= 100);
    ASSERT_EQ(0, Retard[2]);
    LesActions[2].tOnOff = 0;
    demand(255);
    for (int j = 1; j < 5; j++) ASSERT_TRUE(Retard[j] >= 0 && Retard[j] <= 100);
}

TEST_CASE(Sequenceur, SommeDesPuissancesSansDebordementEntier) {
    reset();
    for (int j = 2; j < 5; j++) LesActions[j].PuissanceCharge = 1000000000;
    demand(50);
    ASSERT_EQ(0, Retard[2]);
    ASSERT_EQ(50, Retard[3]);
    ASSERT_EQ(100, Retard[4]);
    ASSERT_NEAR(50, RetardF[1], 0.001);
}

TEST_CASE(Sequenceur, PIDCACSIPlafondEtPasDeRetroactionDuForcage) {
    reset();
    LesActions[1].Type[0] = 4;
    LesActions[1].Vmin[0] = 0;
    LesActions[1].Vmax[0] = 70;
    LesActions[1].Ki = 10;
    IntegrErrorPw[1] = 50;
    PuissanceI_M = 1000;
    ReacCACSI = 4;
    GestionOverproduction();
    ASSERT_NEAR(46.0001, IntegrErrorPw[1], 0.001);
    ASSERT_NEAR(46.0001, RetardF[1], 0.001);
    ModePara = 1;
    LesActions[1].PID = true;
    LesActions[1].Kp = LesActions[1].Kd = 10;
    PuissanceI_M = 0;
    PuissanceS_M = 200;
    GestionOverproduction();
    ASSERT_NEAR(50.6002, RetardF[1], 0.001);
    LesActions[4].tOnOff = 60;
    GestionOverproduction();
    const float integrator = IntegrErrorPw[1];
    ASSERT_TRUE(RetardF[1] < integrator);
    LesActions[4].tOnOff = 0;
    GestionOverproduction();
    ASSERT_NEAR(integrator + 0.2001, IntegrErrorPw[1], 0.001);
    PuissanceS_M = 0;
    PuissanceI_M = 100000;
    GestionOverproduction();
    ASSERT_NEAR(30, RetardF[1], 0.001);
}

TEST_CASE(Sequenceur, ReconfigurationEtDiagnosticSansFlood) {
    reset();
    demand(100);
    LesActions[3].IdxSequenceur = 99;
    GestionOverproduction();
    const size_t count = messages.size();
    ASSERT_TRUE(count > 0);
    GestionOverproduction();
    ASSERT_EQ(count, messages.size());
    LesActions[3].IdxSequenceur = 1;
    LesActions[4].PuissanceCharge = 2000;
    demand(50);
    ASSERT_EQ(0, Retard[2]);
    ASSERT_EQ(0, Retard[3]);
    ASSERT_EQ(100, Retard[4]);
    RetardVx = 3;
    GestionOverproduction();
    ASSERT_TRUE(messages.back().find("Retard= 0") != std::string::npos);
    LesActions[0].Actif = MODE_SEQUENCEUR;
    LesActions[0].ONouvre[0] = 100;
    GestionOverproduction();
    ASSERT_EQ(100, Retard[0]);
}
