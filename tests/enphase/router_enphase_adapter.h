#pragma once

#include "../harness/mock_arduino.h"
#include "../harness/test_runner.h"

// -----------------------------------------------------------------------------
// État global émulé du Solar Router en mode Enphase
// -----------------------------------------------------------------------------

inline unsigned long RMSextIP = 0xC0A80164; // 192.168.1.100
inline bool RMSextIPauto = false;
inline String EnphaseUser = "test@example.com";
inline String EnphasePwd = "secret_password";
inline String EnphaseSerial = "122200000000";
inline String TokenEnphase = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.dummy_token_valid_over_50_chars_for_tests_enphase_envoy";
inline String Session_id = "dummy_session_id";
inline String JsonToken = "";
inline String hostname = "solar-router";

inline float PactProd = 0.0f;
inline float PactReseau = 0.0f;
inline float PactConso_M = 0.0f;
inline float Tension_M = 0.0f;
inline float Intensite_M = 0.0f;
inline float Frequence = 50.0f;

inline float Tension_M1 = 0.0f;
inline float Tension_M2 = 0.0f;
inline float Tension_M3 = 0.0f;
inline float Intensite_M1 = 0.0f;
inline float Intensite_M2 = 0.0f;
inline float Intensite_M3 = 0.0f;

inline long LastwhDlvdCum = 0L;
inline long LastwhRcvdCum = 0L;
inline long Energie_M_Soutiree = 0L;
inline long Energie_M_Injectee = 0L;

inline int PuissanceS_M = 0;
inline int PuissanceI_M = 0;
inline int PVAS_M = 0;
inline int PVAI_M = 0;

inline float PuissanceS_M_inst = 0.0f;
inline float PuissanceI_M_inst = 0.0f;
inline float PVAS_M_inst = 0.0f;
inline float PVAI_M_inst = 0.0f;

inline float Puissance_M_moy = 0.0f;
inline float PVA_M_moy = 0.0f;
inline float PowerFactor_M = 1.0f;

inline int PuissanceS_T = 0;
inline int PuissanceI_T = 0;
inline float PuissanceS_T_inst = 0.0f;
inline float PuissanceI_T_inst = 0.0f;
inline float Puissance_T_moy = 0.0f;
inline int PVAS_T = 0;
inline int PVAI_T = 0;
inline float PVAS_T_inst = 0.0f;
inline float PVAI_T_inst = 0.0f;
inline float PVA_T_moy = 0.0f;

inline bool Pva_valide = false;
inline bool EnergieActiveValide = false;
inline bool PuissanceRecue = false;
inline int cptLEDyellow = 0;
inline bool LissageLong = true;
inline float PmaxReseau = 36000.0f; // 36 kW max

inline unsigned long previousTimeRMSMin = 1000;
inline unsigned long previousTimeRMSMax = 1;
inline unsigned long previousTimeRMSMoy = 1;
inline unsigned long previousTimeRMS = 0;
inline unsigned long LastRMS_Millis = 0;
inline unsigned long PeriodeProgMillis = 2000;

inline unsigned long g_lastGoodReading = 0;

// -----------------------------------------------------------------------------
// Fonctions extraites fidèlement de Source_EnphaseEnvoy.ino & Stockage.ino
// -----------------------------------------------------------------------------

inline float PfloatMax(float Pin) {
    return constrain(Pin, float(-1.0f * PmaxReseau), float(PmaxReseau));
}

inline float ValJson(String nom, String Json) {
    int p = Json.indexOf(nom + "\":");
    Json = Json.substring(p);
    p = Json.indexOf(":");
    Json = Json.substring(p + 1);
    int q = Json.indexOf(",");
    if (q == -1) q = (int)Json.length();
    p = Json.indexOf("}");
    if (p > 0)
        p = std::min(p, q);
    else
        p = q;
    float val = 0;
    if (p > 0) {
        Json = Json.substring(0, p);
        val = Json.toFloat();
    }
    return val;
}

inline long LongJson(String nom, String Json) {
    int p = Json.indexOf(nom + "\":");
    Json = Json.substring(p);
    p = Json.indexOf(":");
    Json = Json.substring(p + 1);
    int q = Json.indexOf(".");
    if (q == -1) {
        q = Json.indexOf(",");
    }
    if (q == -1) {
        q = (int)Json.length();
    }
    p = Json.indexOf("}");
    if (p > 0)
        p = std::min(p, q);
    else
        p = q;
    long val = 0;
    if (p > 0) {
        Json = Json.substring(0, p);
        val = Json.toInt();
    }
    return val;
}

inline String StringJson(String nom, String Json) {
    int p = Json.indexOf(nom + "\":");
    Json = Json.substring(p);
    p = Json.indexOf(":");
    Json = Json.substring(p + 1);
    p = Json.indexOf("\"");
    Json = Json.substring(p + 1);
    p = Json.indexOf("\"");
    Json = Json.substring(0, p);
    return Json;
}

inline void filtre_puissance() {
    float A = 0.3f;
    float B = 0.7f;
    if (!LissageLong) {
        A = 1.0f;
        B = 0.0f;
    }

    Puissance_M_moy = A * (PuissanceS_M_inst - PuissanceI_M_inst) + B * Puissance_M_moy;
    if (Puissance_M_moy < 0) {
        PuissanceI_M = -int(Puissance_M_moy);
        PuissanceS_M = 0;
    } else {
        PuissanceS_M = int(Puissance_M_moy);
        PuissanceI_M = 0;
    }

    PVA_M_moy = A * (PVAS_M_inst - PVAI_M_inst) + B * PVA_M_moy;
    if (PVA_M_moy < 0) {
        PVAI_M = -int(PVA_M_moy);
        PVAS_M = 0;
    } else {
        PVAS_M = int(PVA_M_moy);
        PVAI_M = 0;
    }
}

inline bool JSONReadingEnphase(NetworkClientSecure* pClient, String* pString, char cUntilChar, unsigned long nTimeout) {
    if ((pClient == nullptr) || (pString == nullptr))
        return true;

    pClient->setTimeout(nTimeout);
    unsigned long nT0 = millis();
    *pString = pClient->readStringUntil(cUntilChar);
    return ((millis() - nT0) > nTimeout);
}

// Reproduction exacte du décodage de LectureEnphase() injectée avec un NetworkClientSecure
inline bool ExecLectureEnphase(NetworkClientSecure& client) {
    constexpr unsigned long TIMEOUT_JSON_READING = 100;
    constexpr unsigned long TIMEOUT_WAITING_ANSWER = 500;

    bool bJsonLoadingFinished = false;
    bool bTimeout = false;

    float pActReseau = 0.0f;
    float pVaReseau = 0.0f;
    long whDlvdCum = 0L;
    long whRcvdCum = 0L;

    String jsonPayload;

    auto applique_securite_10s = [&]() {
        if ((millis() - g_lastGoodReading) > 10000) {
            PactProd = 0.0f;
            PactConso_M = 0.0f;
            PactReseau = 0.0f;
            Tension_M = 0.0f;
            Intensite_M = 0.0f;
            Frequence = 0.0f;
            Tension_M1 = 0.0f; Tension_M2 = 0.0f; Tension_M3 = 0.0f;
            Intensite_M1 = 0.0f; Intensite_M2 = 0.0f; Intensite_M3 = 0.0f;
        }
    };

    if (TokenEnphase.length() <= 50 || EnphaseUser == "") {
        StockMessage("Token ou EnphaseUser manquant");
        applique_securite_10s();
        return false;
    }

    // Lecture de la ligne HTTP de réponse
    bTimeout = JSONReadingEnphase(&client, &jsonPayload, '\n', TIMEOUT_WAITING_ANSWER);
    jsonPayload.trim();

    if (bTimeout || (jsonPayload.indexOf("200") < 0)) {
        StockMessage("Envoy refused request");
        client.stop();
        applique_securite_10s();
        return false;
    }

    int nGlobalIndex = 0;
    int nPhaseIndex = 0;
    bool bMonoPhase = true;

    // Saute l'en-tête d'ouverture '['
    bTimeout = JSONReadingEnphase(&client, &jsonPayload, '[', TIMEOUT_JSON_READING);
    if (bTimeout) {
        StockMessage("JSON Reading Timeout 1");
        applique_securite_10s();
        return false;
    }

    for (nGlobalIndex = 0; (nGlobalIndex < 8) && !bJsonLoadingFinished && !bTimeout; nGlobalIndex++) {
        bTimeout = JSONReadingEnphase(&client, &jsonPayload, '[', TIMEOUT_JSON_READING);
        if (bTimeout) {
            StockMessage("JSON Reading Timeout 2 / nGlobalIndex=" + String(nGlobalIndex) + " nPhaseIndex=" + String(nPhaseIndex));
            applique_securite_10s();
            return false;
        }

        if (nGlobalIndex == 0) {
            float tension = ValJson("voltage", jsonPayload);
            if (tension > 280.0f)
                bMonoPhase = false;

            if (!bMonoPhase) {
                PactProd = ValJson("activePower", jsonPayload);
                Tension_M = ValJson("voltage", jsonPayload);
                Intensite_M = ValJson("current", jsonPayload);
            }
        } else if (nGlobalIndex == 1) {
            if (!bMonoPhase) {
                pActReseau = ValJson("activePower", jsonPayload);
                PactConso_M = pActReseau + PactProd;
                pVaReseau = ValJson("apparentPower", jsonPayload);
                whDlvdCum = (long)ValJson("actEnergyDlvd", jsonPayload);
                whRcvdCum = (long)ValJson("actEnergyRcvd", jsonPayload);
                Frequence = ValJson("freq", jsonPayload);
            }
        }

        for (nPhaseIndex = 0; (nPhaseIndex < 3) && !bJsonLoadingFinished && !bTimeout; nPhaseIndex++) {
            bTimeout = JSONReadingEnphase(&client, &jsonPayload, '}', TIMEOUT_JSON_READING);
            if (bTimeout) {
                StockMessage("JSON Reading Timeout 3 / nGlobalIndex=" + String(nGlobalIndex) + " nPhaseIndex=" + String(nPhaseIndex));
                applique_securite_10s();
                return false;
            }
            jsonPayload += "}";

            if ((nGlobalIndex == 0) && (nPhaseIndex == 0)) {
                if (bMonoPhase) {
                    PactProd = ValJson("activePower", jsonPayload);
                }
            } else if ((nGlobalIndex == 1) && (nPhaseIndex == 0)) {
                Tension_M1 = ValJson("voltage", jsonPayload);
                Intensite_M1 = ValJson("current", jsonPayload);

                if (bMonoPhase) {
                    pActReseau = ValJson("activePower", jsonPayload);
                    PactConso_M = pActReseau + PactProd;
                    pVaReseau = ValJson("apparentPower", jsonPayload);
                    whDlvdCum = (long)ValJson("actEnergyDlvd", jsonPayload);
                    whRcvdCum = (long)ValJson("actEnergyRcvd", jsonPayload);
                    Frequence = ValJson("freq", jsonPayload);

                    Tension_M = Tension_M1;
                    Intensite_M = Intensite_M1;
                }
            } else if ((nGlobalIndex == 1) && (nPhaseIndex == 1)) {
                Tension_M2 = ValJson("voltage", jsonPayload);
                Intensite_M2 = ValJson("current", jsonPayload);
            } else if ((nGlobalIndex == 1) && (nPhaseIndex == 2)) {
                Tension_M3 = ValJson("voltage", jsonPayload);
                Intensite_M3 = ValJson("current", jsonPayload);

                bJsonLoadingFinished = true;
                g_lastGoodReading = millis();
            }
        }
    }

    if (!bJsonLoadingFinished) {
        if ((millis() - g_lastGoodReading) > 10000) {
            PactProd = 0.0f;
            PactConso_M = 0.0f;
            PactReseau = 0.0f;
            Tension_M = 0.0f;
            Intensite_M = 0.0f;
            Frequence = 0.0f;
            Tension_M1 = 0.0f;
            Tension_M2 = 0.0f;
            Tension_M3 = 0.0f;
            Intensite_M1 = 0.0f;
            Intensite_M2 = 0.0f;
            Intensite_M3 = 0.0f;
        }
        StockMessage("JSON Loading failed");
        return false;
    }

    PactReseau = PfloatMax(pActReseau);
    if (PactReseau < 0) {
        PuissanceS_M_inst = 0.0f;
        PuissanceI_M_inst = float(-PactReseau);
    } else {
        PuissanceI_M_inst = 0.0f;
        PuissanceS_M_inst = float(PactReseau);
    }

    pVaReseau = PfloatMax(pVaReseau);
    if (PactReseau < 0) {
        PVAS_M_inst = 0.0f;
        PVAI_M_inst = float(pVaReseau);
    } else {
        PVAI_M_inst = 0.0f;
        PVAS_M_inst = float(pVaReseau);
    }
    Pva_valide = true;

    filtre_puissance();

    float powerFactor = 0.0f;
    if (PVA_M_moy != 0) {
        powerFactor = std::floor(100.0f * std::fabs(Puissance_M_moy) / PVA_M_moy) / 100.0f;
        powerFactor = std::min(powerFactor, 1.0f);
    }
    PowerFactor_M = powerFactor;

    if (whDlvdCum != 0) {
        if (LastwhDlvdCum == 0)
            LastwhDlvdCum = whDlvdCum;
        long deltaWhSoutire = whDlvdCum - LastwhDlvdCum;
        LastwhDlvdCum = whDlvdCum;
        if (deltaWhSoutire > 0) {
            Energie_M_Soutiree += deltaWhSoutire;
        }
    }

    if (whRcvdCum != 0) {
        if (LastwhRcvdCum == 0)
            LastwhRcvdCum = whRcvdCum;
        long deltaWhInjecte = whRcvdCum - LastwhRcvdCum;
        LastwhRcvdCum = whRcvdCum;
        if (deltaWhInjecte > 0) {
            Energie_M_Injectee += deltaWhInjecte;
        }
    }

    EnergieActiveValide = true;
    if (PactReseau != 0 || pVaReseau != 0) PuissanceRecue = true;
    if (cptLEDyellow > 30) cptLEDyellow = 4;

    return true;
}

inline void reset_router_state() {
    g_simulated_millis = 1000;
    g_lastGoodReading = 1000;
    TokenEnphase = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.dummy_token_valid_over_50_chars_for_tests_enphase_envoy";
    PactProd = 0.0f;
    PactReseau = 0.0f;
    PactConso_M = 0.0f;
    Tension_M = 0.0f;
    Intensite_M = 0.0f;
    Frequence = 50.0f;
    Tension_M1 = 0.0f; Tension_M2 = 0.0f; Tension_M3 = 0.0f;
    Intensite_M1 = 0.0f; Intensite_M2 = 0.0f; Intensite_M3 = 0.0f;
    LastwhDlvdCum = 0L;
    LastwhRcvdCum = 0L;
    Energie_M_Soutiree = 0L;
    Energie_M_Injectee = 0L;
    PuissanceS_M = 0;
    PuissanceI_M = 0;
    PuissanceS_M_inst = 0.0f;
    PuissanceI_M_inst = 0.0f;
    Puissance_M_moy = 0.0f;
    PVAS_M = 0;
    PVAI_M = 0;
    PVAS_M_inst = 0.0f;
    PVAI_M_inst = 0.0f;
    PVA_M_moy = 0.0f;
    PowerFactor_M = 1.0f;
    Pva_valide = false;
    EnergieActiveValide = false;
    PuissanceRecue = false;
    LissageLong = true;
    g_stockMessages.clear();
}
