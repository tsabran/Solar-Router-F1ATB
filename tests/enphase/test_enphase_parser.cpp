#include <fstream>
#include "../harness/test_runner.h"
#include "router_enphase_adapter.h"

static std::string read_fixture_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw TestHarness::TestFailure{"Impossible d'ouvrir le fichier fixture: " + path, __FILE__, __LINE__};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

TEST_CASE(Enphase, TestHelpersJson) {
    String sample = "{\"voltage\":234.56,\"activePower\":-1500.5,\"eid\":12345,\"name\":\"envoy\"}";
    
    float v = ValJson("voltage", sample);
    ASSERT_NEAR(234.56f, v, 0.01f);

    float p = ValJson("activePower", sample);
    ASSERT_NEAR(-1500.5f, p, 0.01f);

    long id = LongJson("eid", sample);
    ASSERT_EQ(12345L, id);

    String name = StringJson("name", sample);
    ASSERT_EQ("envoy", name);
}

TEST_CASE(Enphase, LectureMonophaseSurplus) {
    reset_router_state();

    std::string jsonBody = read_fixture_file("tests/enphase/fixtures/envoy_readings_monophase_surplus.json");
    std::string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + jsonBody;

    NetworkClientSecure client;
    client.feedData(httpResponse);

    bool ok = ExecLectureEnphase(client);
    ASSERT_TRUE(ok);

    // Vérifications métriques solaires et réseau
    ASSERT_NEAR(2500.0f, PactProd, 1.0f);
    ASSERT_NEAR(-1800.0f, PactReseau, 1.0f);
    ASSERT_NEAR(700.0f, PactConso_M, 1.0f); // 2500W produit - 1800W injecté = 700W consommé

    // Signe des puissances instantanées : Réseau < 0 => Injection
    ASSERT_EQ(0.0f, PuissanceS_M_inst); // Pas de soutirage
    ASSERT_NEAR(1800.0f, PuissanceI_M_inst, 1.0f); // 1800W injectés

    // Tension et Fréquence
    ASSERT_NEAR(232.5f, Tension_M, 0.5f);
    ASSERT_NEAR(50.0f, Frequence, 0.1f);
    ASSERT_TRUE(PuissanceRecue);
    ASSERT_TRUE(EnergieActiveValide);
}

TEST_CASE(Enphase, LectureMonophaseSoutirage) {
    reset_router_state();

    std::string jsonBody = read_fixture_file("tests/enphase/fixtures/envoy_readings_monophase_import.json");
    std::string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + jsonBody;

    NetworkClientSecure client;
    client.feedData(httpResponse);

    bool ok = ExecLectureEnphase(client);
    ASSERT_TRUE(ok);

    // Production faible, forte consommation réseau
    ASSERT_NEAR(350.0f, PactProd, 1.0f);
    ASSERT_NEAR(1650.0f, PactReseau, 1.0f);
    ASSERT_NEAR(2000.0f, PactConso_M, 1.0f); // 1650W réseau + 350W solaire = 2000W conso maison

    // Signe des puissances instantanées : Réseau > 0 => Soutirage
    ASSERT_NEAR(1650.0f, PuissanceS_M_inst, 1.0f); // 1650W soutirés
    ASSERT_EQ(0.0f, PuissanceI_M_inst); // Pas d'injection
}

TEST_CASE(Enphase, LectureTriphase) {
    reset_router_state();

    std::string jsonBody = read_fixture_file("tests/enphase/fixtures/envoy_readings_triphase_surplus.json");
    std::string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + jsonBody;

    NetworkClientSecure client;
    client.feedData(httpResponse);

    bool ok = ExecLectureEnphase(client);
    ASSERT_TRUE(ok);

    // Détection triphasée (Tension composite > 280V)
    ASSERT_NEAR(6000.0f, PactProd, 1.0f);
    ASSERT_NEAR(-3600.0f, PactReseau, 1.0f);
    ASSERT_NEAR(2400.0f, PactConso_M, 1.0f); // 6000W - 3600W

    // Mesures individuelles des 3 phases
    ASSERT_NEAR(231.5f, Tension_M1, 0.5f);
    ASSERT_NEAR(232.0f, Tension_M2, 0.5f);
    ASSERT_NEAR(231.9f, Tension_M3, 0.5f);

    ASSERT_NEAR(5.2f, Intensite_M1, 0.2f);
    ASSERT_NEAR(5.1f, Intensite_M2, 0.2f);
    ASSERT_NEAR(5.3f, Intensite_M3, 0.2f);
}

TEST_CASE(Enphase, AccumulationCompteursEnergieWh) {
    reset_router_state();

    std::string jsonBody = read_fixture_file("tests/enphase/fixtures/envoy_readings_monophase_surplus.json");
    std::string httpResponse1 = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + jsonBody;

    // 1ere lecture : initialise les compteurs de base
    NetworkClientSecure client1;
    client1.feedData(httpResponse1);
    ExecLectureEnphase(client1);
    ASSERT_EQ(0L, Energie_M_Injectee);
    ASSERT_EQ(0L, Energie_M_Soutiree);

    // 2eme lecture : l'index d'injection a augmenté de 50 Wh (320000 -> 320050)
    std::string jsonBody2 = jsonBody;
    size_t pos = jsonBody2.find("320000.0");
    while (pos != std::string::npos) {
        jsonBody2.replace(pos, 8, "320050.0");
        pos = jsonBody2.find("320000.0");
    }

    std::string httpResponse2 = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + jsonBody2;
    NetworkClientSecure client2;
    client2.feedData(httpResponse2);
    ExecLectureEnphase(client2);

    ASSERT_EQ(50L, Energie_M_Injectee);
    ASSERT_EQ(0L, Energie_M_Soutiree);
}

TEST_CASE(Enphase, DynamiqueFiltreRC) {
    reset_router_state();
    LissageLong = true; // A = 0.3, B = 0.7

    // Échelon de puissance : 0 -> 1000 W soutirés
    PuissanceS_M_inst = 1000.0f;
    PuissanceI_M_inst = 0.0f;

    // Cycle 1
    filtre_puissance();
    // 0.3 * 1000 + 0.7 * 0 = 300
    ASSERT_EQ(300, PuissanceS_M);

    // Cycle 2
    filtre_puissance();
    // 0.3 * 1000 + 0.7 * 300 = 510
    ASSERT_EQ(510, PuissanceS_M);

    // Cycle 3
    filtre_puissance();
    // 0.3 * 1000 + 0.7 * 510 = 657
    ASSERT_EQ(657, PuissanceS_M);

    // Sans lissage (LissageLong = false) : réponse instantanée
    LissageLong = false;
    filtre_puissance();
    ASSERT_EQ(1000, PuissanceS_M);
}

TEST_CASE(Enphase, WatchdogSecuritePerteEnvoy10s) {
    reset_router_state();

    // 1. Une lecture réussie
    std::string jsonBody = read_fixture_file("tests/enphase/fixtures/envoy_readings_monophase_surplus.json");
    std::string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + jsonBody;

    NetworkClientSecure client;
    client.feedData(httpResponse);
    ExecLectureEnphase(client);
    ASSERT_NEAR(2500.0f, PactProd, 1.0f);

    // 2. Échec de lecture au bout de 5 secondes (< 10s) : le réseau échoue avant de charger la conso
    advance_simulated_millis(5000);
    NetworkClientSecure clientFail1;
    // Fournit l'objet 0 mais coupe avant l'objet 1 (réseau)
    clientFail1.feedData("HTTP/1.1 200 OK\r\n\r\n[ [ {\"activePower\": 2500.0} } } }");
    bool ok1 = ExecLectureEnphase(clientFail1);
    ASSERT_FALSE(ok1);
    ASSERT_NEAR(2500.0f, PactProd, 1.0f); // Toujours conservé car < 10s écoulées

    // 3. Échec après plus de 10 secondes (> 10s sans lecture complète valide) :
    //    Sécurité ! Remise à zéro totale de toutes les mesures pour éviter de sur-injecter/sur-consommer
    advance_simulated_millis(6000); // 5000 + 6000 = 11000 ms écoulées depuis la dernière bonne lecture
    NetworkClientSecure clientFail2;
    clientFail2.feedData("HTTP/1.1 200 OK\r\n\r\n[ [ {\"activePower\": 2500.0} } } }");
    bool ok2 = ExecLectureEnphase(clientFail2);
    ASSERT_FALSE(ok2);

    ASSERT_EQ(0.0f, PactProd);
    ASSERT_EQ(0.0f, PactReseau);
    ASSERT_EQ(0.0f, PactConso_M);
    ASSERT_EQ(0.0f, Tension_M);
    ASSERT_EQ(0.0f, Intensite_M);
}

TEST_CASE(Enphase, ErreurHttpEnvoyRefuse) {
    reset_router_state();

    NetworkClientSecure client;
    client.feedData("HTTP/1.1 401 Unauthorized\r\n\r\n{\"error\":\"Invalid token\"}");

    bool ok = ExecLectureEnphase(client);
    ASSERT_FALSE(ok);
    ASSERT_TRUE(!g_stockMessages.empty());
    ASSERT_EQ("Envoy refused request", g_stockMessages.back());
}

// Capture réelle anonymisée : 8 compteurs, JSON indenté, Content-Length.
TEST_CASE(Enphase, LectureReponseReelleHuitCompteurs) {
    reset_router_state();

    std::string jsonBody = read_fixture_file("tests/enphase/fixtures/envoy_readings_real_8meters.json");
    std::string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                               std::to_string(jsonBody.size()) + "\r\n\r\n" + jsonBody;

    NetworkClientSecure client;
    client.feedData(httpResponse);

    ASSERT_TRUE(ExecLectureEnphase(client));
    ASSERT_NEAR(4022.4f, PactProd, 1.0f);
    ASSERT_NEAR(-92.1f, PactReseau, 1.0f);
    ASSERT_TRUE(PuissanceRecue);
}
