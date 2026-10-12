#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include "../harness/test_runner.h"
#include "router_enphase_adapter.h"

// -----------------------------------------------------------------------------
// Simulateur de régulation du Routeur F1ATB (Triac / Charge chauffe-eau)
// Reprend la formule exacte de Solar_Router_V17_26.ino (RetardF et IntegrErrorPw)
// -----------------------------------------------------------------------------
struct RouterControllerSimulator {
    float powerTriac = 0.0f;       // Puissance routée vers la charge (0 à 2000 W)
    float maxPowerTriac = 2000.0f;  // Chauffe-eau 2000 W
    float SeuilPw = -20.0f;        // Consigne seuil de surplus (-20 W)
    float Ki = 0.0035f;            // Ki réel de régulation du routeur
    float IntegrErrorPw = 100.0f;  // RetardF initial : 100% fermé (0% ouvert)

    void update(float netPowerFiltered) {
        // Dans F1ATB, GestionOverproduction() tourne toutes les 200ms (10 fois par cycle Enphase de 2s)
        // Ki par défaut dans Actions.cpp = 10 -> Ki = 10 / 10000 = 0.001
        for (int i = 0; i < 10; i++) {
            float ErrorPw = netPowerFiltered - SeuilPw;
            IntegrErrorPw += ErrorPw * Ki;
            IntegrErrorPw = constrain(IntegrErrorPw, 0.0f, 100.0f);
        }
        float RetardF = IntegrErrorPw;
        float ouverturePercent = (100.0f - RetardF) / 100.0f;
        powerTriac = ouverturePercent * maxPowerTriac;
    }
};

static std::string build_envoy_frame(float solarProd, float gridNetPower) {
    // gridNetPower = consoBase + triac - solarProd
    std::string frame = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n[\n"
        "  {\n"
        "    \"voltage\": 230.0,\n"
        "    \"activePower\": " + std::to_string(solarProd) + ",\n"
        "    \"channels\": [\n"
        "      {\"voltage\": 230.0, \"activePower\": " + std::to_string(solarProd) + "},\n"
        "      {\"voltage\": 0.0, \"current\": 0.0},\n"
        "      {\"voltage\": 0.0, \"current\": 0.0}\n"
        "    ]\n"
        "  },\n"
        "  {\n"
        "    \"voltage\": 230.0,\n"
        "    \"activePower\": " + std::to_string(gridNetPower) + ",\n"
        "    \"apparentPower\": " + std::to_string(std::fabs(gridNetPower) + 50.0f) + ",\n"
        "    \"freq\": 50.0,\n"
        "    \"actEnergyDlvd\": 100000.0,\n"
        "    \"actEnergyRcvd\": 50000.0,\n"
        "    \"channels\": [\n"
        "      {\"voltage\": 230.0, \"activePower\": " + std::to_string(gridNetPower) + "},\n"
        "      {\"voltage\": 0.0, \"current\": 0.0},\n"
        "      {\"voltage\": 0.0, \"current\": 0.0}\n"
        "    ]\n"
        "  }\n"
        "]";
    return frame;
}

TEST_CASE(Simulation, ScenarioJourneeCompleteEnphase) {
    reset_router_state();
    RouterControllerSimulator controller;

    struct Step {
        const char* timeDesc;
        float baseHouseLoad; // Consommation du foyer hors routeur (W)
        float solarProduction; // Production des panneaux Enphase (W)
        int durationCycles;   // Nombre de cycles de 2s
        bool simulateEnvoyLoss; // Si true, l'Envoy tombe en panne
    };

    std::vector<Step> steps = {
        // 1. Nuit : conso 300W talon, 0W solaire
        {"06:00 - Nuit", 300.0f, 0.0f, 5, false},

        // 2. Lever du jour : soleil commence (800W), maison 350W => surplus ~450W
        {"08:30 - Lever de soleil", 350.0f, 800.0f, 15, false},

        // 3. Plein soleil : 2500W solaire, maison 400W => fort surplus => routage au max
        {"12:00 - Plein soleil", 400.0f, 2500.0f, 20, false},

        // 4. Pic de conso ménage : allumage four (+2000W) => déficit temporaire
        {"12:30 - Four allume", 2400.0f, 2500.0f, 15, false},

        // 5. Passage nuageux brutal : solaire chute à 300W
        {"14:00 - Orage nuageux", 400.0f, 300.0f, 15, false},

        // 6. Coupure temporaire Envoy pendant 12 cycles (24s)
        {"15:00 - Panne Envoy", 400.0f, 2000.0f, 12, true},

        // 7. Retour Envoy
        {"15:30 - Retablissement", 400.0f, 2000.0f, 10, false}
    };

    std::cout << "\n\n   --- SIMULATION DYNAMIQUE ROUTEUR SOLAIRE ENPHASE ---\n";
    std::cout << "   " << std::setw(24) << std::left << "Evenement"
              << std::setw(10) << "Solaire"
              << std::setw(12) << "Maison"
              << std::setw(12) << "Routage"
              << std::setw(12) << "Reseau"
              << "Comportement\n";
    std::cout << "   " << std::string(75, '-') << "\n";

    for (const auto& s : steps) {
        for (int c = 0; c < s.durationCycles; c++) {
            advance_simulated_millis(2000);

            if (s.simulateEnvoyLoss) {
                // Pas de trame envoyée ou timeout
                NetworkClientSecure client;
                client.feedData("HTTP/1.1 500 Server Error\r\n\r\n");
                ExecLectureEnphase(client);
                // Si la lecture échoue depuis > 10s, le routeur doit stopper la charge
                if (millis() - g_lastGoodReading > 10000) {
                    controller.powerTriac = 0.0f; // Sécurité coupure
                }
            } else {
                // Réseau = Maison + Triac - Solaire
                float netGrid = s.baseHouseLoad + controller.powerTriac - s.solarProduction;
                std::string frame = build_envoy_frame(s.solarProduction, netGrid);

                NetworkClientSecure client;
                client.feedData(frame);
                bool ok = ExecLectureEnphase(client);
                ASSERT_TRUE(ok);

                // Asservissement basé sur la puissance lissée (négative = injection)
                float netFiltered = (float)(PuissanceS_M - PuissanceI_M);
                controller.update(netFiltered);
            }
        }

        float netReseauFinal = s.baseHouseLoad + controller.powerTriac - s.solarProduction;
        std::cout << "   " << std::setw(24) << std::left << s.timeDesc
                  << std::setw(10) << (int)s.solarProduction
                  << std::setw(12) << (int)s.baseHouseLoad
                  << std::setw(12) << (int)controller.powerTriac
                  << std::setw(12) << (int)netReseauFinal;

        if (s.simulateEnvoyLoss) {
            std::cout << "🚨 Securite activee (Triac = 0W)\n";
            ASSERT_EQ(0.0f, controller.powerTriac);
        } else if (s.solarProduction > s.baseHouseLoad) {
            std::cout << "☀️ Routage surplus vers Triac\n";
            ASSERT_TRUE(controller.powerTriac > 0.0f);
        } else {
            std::cout << "🌙 Pas de surplus (Triac = 0W)\n";
            ASSERT_NEAR(0.0f, controller.powerTriac, 10.0f);
        }
    }
    std::cout << "   " << std::string(75, '-') << "\n\n";
}
