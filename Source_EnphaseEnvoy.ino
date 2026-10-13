//***********************************
//* Source EnPhase V7   			*
//***********************************

#include "EnvoyEnergySample.h"

uint32_t ipToInt(IPAddress ip) {                                                                  //SR19
  return uint32_t(ip[0] << 24) | uint32_t(ip[1] << 16) | uint32_t(ip[2] << 8) | uint32_t(ip[3]);  //SR19
}

int HttpStatusCode(const char* statusLine);

// Même fichier et même format que la V17.30 upstream (MC001) : le token survit aux redémarrages,
// ce qui évite deux connexions TLS vers Enlighten à chaque boot et permet de démarrer sans cloud.
#define ENPHASE_TOKEN_FILE "/tokenenphase.json"
#define TOKEN_MIN_LENGTH 200
#define ENPHASE_CLOUD_TIMEOUT_CONNECT 4000

void SaveTokenEnphase() {
  File f = LittleFS.open(ENPHASE_TOKEN_FILE, FILE_WRITE);
  if (!f) {
    StockMessage("Enphase token save failed");
    return;
  }
  f.print(String("{\"token\":\"") + TokenEnphase + "\"}");
  f.close();
}

bool LoadTokenEnphase() {
  if (!LittleFS.exists(ENPHASE_TOKEN_FILE)) return false;
  File f = LittleFS.open(ENPHASE_TOKEN_FILE, FILE_READ);
  if (!f) return false;
  String json = f.readString();
  f.close();
  JsonDocument document;
  if (deserializeJson(document, json.c_str()) || !document["token"].is<const char*>()) return false;
  String token = document["token"].as<const char*>();
  if (token.length() <= TOKEN_MIN_LENGTH) return false;
  TokenEnphase = token;
  return true;
}

bool ReadEnphaseCloudResponse(NetworkClientSecure& client, String& body) {
  String response;
  const unsigned long started = millis();
  while (client.connected() || client.available()) {
    while (client.available()) {
      if (response.length() >= 4096) {
        StockMessage("Enphase cloud response too long");
        client.stop();
        return false;
      }
      response += char(client.read());
    }
    if ((millis() - started) >= 5000) {
      StockMessage("Enphase cloud response timeout");
      client.stop();
      return false;
    }
    yield();
  }
  client.stop();
  int separator = response.indexOf("\r\n\r\n");
  int statusEnd = response.indexOf("\r\n");
  int codeStart = response.indexOf(' ');
  if (strncmp(response.c_str(), "HTTP/", 5) != 0 || separator < 0 || statusEnd < 0 ||
      codeStart < 0 || codeStart >= statusEnd ||
      HttpStatusCode(response.c_str()) != 200) {
    StockMessage("Enphase cloud invalid HTTP response");
    return false;
  }
  body = response.substring(separator + 4);
  return true;
}

void Setup_Enphase(bool NewToken) {
  // Un token expiré sera refusé (401/403) par l'Envoy puis renouvelé par LectureEnphase().
  const bool tokenLoaded = !NewToken && LoadTokenEnphase();
  if (tokenLoaded) StockMessage("Enphase token loaded from flash (" + String(TokenEnphase.length()) + ")");

  //Résolution mDNS de http://envoy.local en adresse IP                                                                  //SR19
  //***************************************************                                                                  //SR19

  const char* host = "envoy";  //SR19
  IPAddress envoyIP;
  if (RMSextIPauto) {                                                                                //SR19
    if (!MDNS.begin(hostname)) {                                                                     //Init mDNS                                                                              //SR19
      StockMessage("Erreur : impossible d'initialiser mDNS");                                       //SR19
      return;                                                                                        //SR19
    } else {                                                                                         //SR19
      envoyIP = MDNS.queryHost(host, 2000);                                                          //avec timeout 2s                                                             //SR19
    }                                                                                                //SR19
    if (envoyIP.toString() != "0.0.0.0") {                                                           //SR19
      StockMessage("IP Enphase : http://" + String(host) + ".local" + " -> " + envoyIP.toString());  //SR19
      RMSextIP = ipToInt(envoyIP);  
      EcritureEnROM();                                              //IP -> uint32                                                                         //SR19
    } else {                                                        //SR19
      StockMessage("Échec! passerelle Enphase envoy déconnectée");  //SR19
      return;                                                       //SR19
    }
  }
  if (tokenLoaded) return;
  //Obtention Session ID
  //********************
  const char* server1Enphase = "enlighten.enphaseenergy.com";
  String Host = String(server1Enphase);
  String adrEnphase = "https://" + Host + "/login/login.json";
  String requestBody = "user[email]=" + EnphaseUser + "&user[password]=" + urlEncode(EnphasePwd);
  String Session_id = "";
  String JsonToken = "";

  if (EnphaseUser != "" && EnphasePwd != "" && RMSextIP > 0) {  // test envoyIP si perte de connexion //SR19
    TelnetPrintln("Essai connexion  Enlighten server 1 pour obtention session_id!");
    clientSecu.setInsecure();  //skip verification
    if (!clientSecu.connect(server1Enphase, 443, ENPHASE_CLOUD_TIMEOUT_CONNECT)) {
      StockMessage("Connection failed to Enlighten server :" + Host);
      clientSecu.stop();
      return;
    } else {
      TelnetPrintln("Connected to Enlighten server:" + Host);
	    StockMessage("Connected to Enlighten server :" + Host);
      clientSecu.println("POST " + adrEnphase + "?" + requestBody + " HTTP/1.0");
      clientSecu.println("Host: " + Host);
      clientSecu.println("Connection: close");
      clientSecu.println();
      if (!ReadEnphaseCloudResponse(clientSecu, JsonToken)) return;
    }
    JsonDocument session;
    if (deserializeJson(session, JsonToken.c_str()) || !session["session_id"].is<const char*>()) {
      StockMessage("Enphase cloud invalid session JSON");
      return;
    }
    Session_id = session["session_id"].as<const char*>();
    TelnetPrintln("session_id :" + Session_id);
	  StockMessage("session_id :" + Session_id);
  } 

  //Obtention Token
  //********************
  if (Session_id != "" && EnphaseSerial != "" && EnphaseUser != "") {
    const char* server2Enphase = "entrez.enphaseenergy.com";
    Host = String(server2Enphase);
    adrEnphase = "https://" + Host + "/tokens";
    requestBody = "{\"session_id\":\"" + Session_id + "\", \"serial_num\":" + EnphaseSerial + ", \"username\":\"" + EnphaseUser + "\"}";
    TelnetPrintln("Essai connexion  Enlighten server 2 pour obtention token!");
    clientSecu.setInsecure();  //skip verification
    if (!clientSecu.connect(server2Enphase, 443, ENPHASE_CLOUD_TIMEOUT_CONNECT)) {
      StockMessage("Connection failed to :" + Host);
      clientSecu.stop();
      return;
    } else {
      TelnetPrintln("Connected to :" + Host);
	    StockMessage("Connected to :" + Host);
      clientSecu.println("POST " + adrEnphase + " HTTP/1.0");
      clientSecu.println("Host: " + Host);
      clientSecu.println("Content-Type: application/json");
      clientSecu.println("Content-Length:" + String(requestBody.length()));
      clientSecu.println("Connection: close");
      clientSecu.println();
      clientSecu.println(requestBody);
      clientSecu.println();
      TelnetPrintln("Attente user est connecté");
      if (!ReadEnphaseCloudResponse(clientSecu, JsonToken)) return;
      JsonToken.trim();
      TelnetPrintln("Token :" + JsonToken);
      //StockMessage("Token :" + JsonToken);
      if (JsonToken.length() > 50) {
        TokenEnphase = JsonToken;
        SaveTokenEnphase();
        previousTimeRMSMin = 1000;
        previousTimeRMSMax = 1;
        previousTimeRMSMoy = 1;
        previousTimeRMS = millis();
        LastRMS_Millis = millis();
        PeriodeProgMillis = 1000;
      }
    }
  }
}


// Delai minimum entre deux demandes de token
// (chaque demande = 2 connexions TLS bloquantes vers Enlighten)
constexpr uint32_t TOKEN_RETRY_MS = 5UL * 60UL * 1000UL;

// Le Setup_Enphase() du boot compte comme une tentative (s_lastTry = 0)
bool EnphaseTokenRetryAllowed() {
  static uint32_t s_lastTry = 0;
  if (uint32_t(millis() - s_lastTry) < TOKEN_RETRY_MS) return false;
  s_lastTry = millis();
  return true;
}

void RenouvelleTokenEnphase() {
  String previousToken = TokenEnphase;
  TokenEnphase = "";
  Setup_Enphase(true);
  if (TokenEnphase.length() > 50) {
    EnvoySessionIdCookie = "";
    StockMessage("Enphase token renewed");
  } else {
    TokenEnphase = previousToken;
    StockMessage("Enphase token renewal failed, next attempt in 5 min");
  }
}

// "HTTP/1.1 401 Unauthorized" -> 401, 0 si non reconnu
int HttpStatusCode(const char* statusLine) {
  if (!statusLine) return 0;
  const char* p = strstr(statusLine, "HTTP/");
  if (!p) return 0;
  p = strchr(p, ' ');
  if (!p || strlen(p) < 4 || !isdigit((unsigned char)p[1]) ||
      !isdigit((unsigned char)p[2]) || !isdigit((unsigned char)p[3]) ||
      (p[4] && p[4] != ' ' && p[4] != '\r')) return 0;
  return atoi(p + 1);
}

enum ReadStatus
{
  READ_OK,
  READ_INVALID_ARGUMENT,
  READ_TIMEOUT,
  READ_DISCONNECTED,
  READ_TOO_LONG
};


const char* ReadStatusToString(int status)
{
  switch (status) {
    case READ_OK:
      return "OK";
    case READ_INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case READ_TIMEOUT:
      return "TIMEOUT";
    case READ_DISCONNECTED:
      return "DISCONNECTED";
    case READ_TOO_LONG:
      return "TOO_LONG";
    default:
      return "UNKNOWN";
  }
}

int ReadBufferUntilChar(  // Précedemment appelé JSONReadingEnphase
    NetworkClient& stream,
    char* out,
    size_t maxSize,
    size_t& outLen,
    char untilChar,
    unsigned long timeoutMs)
{
  outLen = 0;
  if (out == nullptr || maxSize == 0) {
    StockMessage("Envoy read buffer invalid");
    return READ_INVALID_ARGUMENT;
  }

  out[0] = '\0';

  const size_t maxLen = maxSize - 1;
  uint32_t lastActivity = millis();

  while (true) {
    while (stream.available() > 0) {
      int c = stream.read();
      if (c < 0) break;

      lastActivity = millis();

      if ((char)c == untilChar) {
        out[outLen] = '\0';
        return READ_OK;
      }

      if (outLen >= maxLen) {
        out[outLen] = '\0';
        return READ_TOO_LONG;
      }

      out[outLen++] = (char)c;
    }

    if (!stream.connected() && stream.available() == 0) {
      out[outLen] = '\0';
      EnvoyCompteErreurConnectionClosed += 1;
      return READ_DISCONNECTED;
    }

    if (uint32_t(millis() - lastActivity) >= timeoutMs) {
      out[outLen] = '\0';
      EnvoyCompteErreurTimeout += 1;
      return READ_TIMEOUT;
    }

    yield();
  }
}


void PurgeClientBuffer(NetworkClientSecure& client)
{
    while (client.available() > 0) {
      (void)client.read();
    }
}







bool CaptureEnvoySessionIdFromHeaders(NetworkClientSecure& pClient, unsigned long nTimeout)
{

  static char httpHeaderBuf[256]; // 'static' pour en faire une variable globale et eviter de saturer la pile
  size_t httpHeaderLength = 0;

  // Seuls les appels locaux Envoy authentifiés par bearer nécessitent la lecture des headers,
  // car ils renvoient le cookie local `sessionId` à réutiliser sur les appels suivants.
  // Une ligne d'en-tête plus longue que le tampon est ignorée (comme avant le durcissement),
  // dans la limite de 8 Ko d'en-têtes trop longs pour ne pas boucler indéfiniment.
  int longHeaderChunks = 0;
  while(true)
  {
    int status = ReadBufferUntilChar(pClient, httpHeaderBuf, sizeof(httpHeaderBuf), httpHeaderLength, '\n', nTimeout);
    const bool lineTooLong = status == READ_TOO_LONG;

    if (status != READ_OK && !lineTooLong) {
      StockMessage(String("Envoy Headers reading failed, status= ") + ReadStatusToString(status) + ", partialLen=" + httpHeaderLength);
      return true;
    }

    // Trim le \r final en place (ReadBufferUntilChar s'arrete sur \n, peut laisser un \r)
    while (httpHeaderLength > 0 &&
           (httpHeaderBuf[httpHeaderLength - 1] == '\r' ||
            httpHeaderBuf[httpHeaderLength - 1] == ' ')) {
      httpHeaderBuf[--httpHeaderLength] = '\0';
    }

    if (httpHeaderLength == 0)
      return false; //all headers have been read

    if (strncasecmp(httpHeaderBuf, "Set-Cookie:", 11) == 0) {
      const char* p = strstr(httpHeaderBuf, "sessionId=");
      if (p) {
        p += 10; // strlen("sessionId=")
        const char* q = strchr(p, ';');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        // Sur une ligne tronquée, la valeur n'est fiable que si son ';' final a été lu.
        if (len > 0 && (q || !lineTooLong)) {
          EnvoySessionIdCookie = String(p, (unsigned int)len);
          StockMessage("Envoy local sessionId successfully cached");
        }
      }
    }

    while (status == READ_TOO_LONG) {
      if (++longHeaderChunks > 32) {
        StockMessage("Envoy headers too long");
        return true;
      }
      status = ReadBufferUntilChar(pClient, httpHeaderBuf, sizeof(httpHeaderBuf), httpHeaderLength, '\n', nTimeout);
    }
    if (status != READ_OK) {
      StockMessage(String("Envoy Headers reading failed, status= ") + ReadStatusToString(status) + ", partialLen=" + httpHeaderLength);
      return true;
    }
  }
}

// Premier caractère non blanc du flux, ou -1 si le flux se termine / expire.
int ReadNextNonSpaceChar(NetworkClient& stream, unsigned long timeoutMs) {
  uint32_t lastActivity = millis();
  while (true) {
    while (stream.available() > 0) {
      int c = stream.read();
      if (c < 0) break;
      lastActivity = millis();
      if (!isspace((unsigned char)c)) return c;
    }
    if (!stream.connected() && stream.available() == 0) return -1;
    if (uint32_t(millis() - lastActivity) >= timeoutMs) return -1;
    yield();
  }
}

bool EnvoyPayloadPrefixValide(const char* payload, const char* prefix) {
  while (*payload && *payload != '{') {
    if (!isspace((unsigned char)*payload)) {
      if (*payload != *prefix || !*prefix) return false;
      ++prefix;
    }
    ++payload;
  }
  return *payload == '{' && !*prefix;
}

bool EnvoyPayloadValide(char* payload, bool header, bool power, bool grid) {
  JsonDocument document;
  DeserializationError error;
  if (header) {
    // Objet fermé en place juste avant "channels" puis restauré : pas de copie du bloc.
    const char* start;
    size_t length;
    if (!EnvoyReadingHeaderBounds(payload, start, length)) return false;
    char* end = payload + (start - payload) + length;
    const char saved[2] = {end[0], end[1]};
    end[0] = '}';
    end[1] = '\0';
    error = deserializeJson(document, start);
    end[0] = saved[0];
    end[1] = saved[1];
  } else {
    const char* object = strchr(payload, '{');
    if (!object) return false;
    error = deserializeJson(document, object);
  }
  if (error || !document.is<JsonObject>()) return false;
  const char* fields[] = {"activePower", "apparentPower", "voltage", "current",
                          "freq", "actEnergyDlvd", "actEnergyRcvd"};
  for (const char* field : fields) {
    JsonVariantConst value = document[field];
    if (!value.isNull() && (!value.is<double>() || !std::isfinite(value.as<double>()) ||
                           !std::isfinite(float(value.as<double>()))))
      return false;
    if ((strcmp(field, "actEnergyDlvd") == 0 || strcmp(field, "actEnergyRcvd") == 0) &&
        !value.isNull() && (value.as<double>() < 0 || value.as<double>() > INT32_MAX))
      return false;
    if ((grid || (power && strcmp(field, "activePower") == 0) ||
         (header && strcmp(field, "voltage") == 0)) && !value.is<double>())
      return false;
  }
  return true;
}

void LectureEnphase()
{
  #define TIMEOUT_READ_PAYLOAD    100
  #define TIMEOUT_WAITING_ANSWER  500
  #define TIMEOUT_CONNECT         3000

  EnvoyCompteTentativesLectureComplete += 1;
  // init variable 
  static uint32_t g_nLastGoodReading = millis();
  bool bJsonLoadingFinished = false;
  // Tous les chemins de sortie, y compris les erreurs reseau, appliquent la securite.
  struct ReadingGuard {
    uint32_t& lastGood;
    bool& complete;
    ~ReadingGuard() {
      if (complete || uint32_t(millis() - lastGood) <= 10000) return;
      PactProd = PactConso_M = Tension_M = Intensite_M = Frequence = PowerFactor_M = 0;
      Tension_M1 = Tension_M2 = Tension_M3 = Intensite_M1 = Intensite_M2 = Intensite_M3 = 0;
      PuissanceS_M_inst = PuissanceI_M_inst = PVAS_M_inst = PVAI_M_inst = 0;
      PuissanceS_M = PuissanceI_M = PVAS_M = PVAI_M = 0;
      Puissance_M_moy = PVA_M_moy = 0;
      Pva_valide = EnergieActiveValide = PuissanceRecue = false;
      StockMessage("Envoy watchdog: no valid reading for 10 s, powers cleared");
    }
  } readingGuard{g_nLastGoodReading, bJsonLoadingFinished};

  float PactReseau = 0.0f;
  float PvaReseau = 0.0f;
  EnvoyReadObserver energyObserver;
  long whDlvdCum = 0L;  // on perd les decimals après la virgule avec un type long
  long whRcvdCum = 0L; 
  
  // Buffers statiques pour host et baseRequest : reconstruits seulement si l'IP change.
  // Evite 7 String temporaires + 1 concat heap à chaque appel (toutes les secondes).
  static char s_hostBuf[16] = {};
  static char s_baseRequestBuf[128] = {};
  static unsigned long s_lastRMSextIP = 0;
  if (s_lastRMSextIP != RMSextIP) {
    s_lastRMSextIP = RMSextIP;
    snprintf(s_hostBuf, sizeof(s_hostBuf), "%u.%u.%u.%u",
      (unsigned)((RMSextIP >> 24) & 0xFF), (unsigned)((RMSextIP >> 16) & 0xFF),
      (unsigned)((RMSextIP >>  8) & 0xFF), (unsigned)( RMSextIP         & 0xFF));
    snprintf(s_baseRequestBuf, sizeof(s_baseRequestBuf),
      "/ivp/meters/readings HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\nConnection: keep-alive\r\n",
      s_hostBuf);
  }
  const char* host = s_hostBuf;
  const char* baseRequest = s_baseRequestBuf;
  static NetworkClientSecure client;

  // Token absent (ex: Enlighten injoignable au boot) : on retente periodiquement
  if (TokenEnphase.length() <= 50 && EnphaseUser != "" && EnphasePwd != "" && EnphaseTokenRetryAllowed()) {
    StockMessage("Enphase token missing, requesting a new one...");
    RenouvelleTokenEnphase();
  }

  if (TokenEnphase.length() > 50 && EnphaseUser != "") 
  {  
    // Connexion pour firmware V7 en https
    // Token propriétaire valable ~1 an : renouvelé uniquement sur refus 401/403 de l'Envoy.

    // `Cookie: sessionId=...` est le cookie de session local de l'Envoy, et non le `session_id` Enlighten/entrez.
    // Il fait gagner environ 25 ms sur chaque appel a la gateway Envoy par rapport a `Authorization: Bearer`.
    unsigned long nLectureStarted = millis();

    for(int retry = 0; retry < 3; retry++) // boucle pour tester d'abord la connexion authentifiée avec le cookie, puis si refusé on retente avec le token
    {
      yield();

      if (client.connected()) {
        if (client.remoteIP().toString() != host || client.remotePort() != 443 ) {
          StockMessage("Envoy connection host or port has changed, recreating connection");
          client.stop();
        } else {
          // on tente de reutiliser la connection ouverte. on purge d'abord le contenu restant dans le buffer.
          // attention: le server peut encore envoyer du contenu pour la requete precedente,
          // il faudra prendre soin d'ignorer ce contenu quand on attendra la reponse de la prochaine requete
          unsigned long nConnectStarted = millis();
          PurgeClientBuffer(client);
          EnvoyDureeDerniereConnexionReutiliseeMs.add(millis() - nConnectStarted);
        }
      }

      if (!client.connected()) 
	    {  // établi la connexion
        TelnetPrintln("Envoy connection is closed. (Re)connecting...");

        // TelnetPrintln("Connecting to Envoy-S server HTTPS...");
        EnvoyCompteTentativesConnexionRenouvelee += 1;
        client.stop();        
        delay(10); // (optionnel) petite pause pour laisser lwIP libérer la socket
        client.setInsecure();     // skip verification
        client.setTimeout(TIMEOUT_CONNECT);
        unsigned long nConnectStarted = millis();
        if (!client.connect(host, 443))
	      {
          StockMessage(String("Connection failed to Envoy-S server! : https://") + host);
          EnvoyCompteErreurEchecConnect += 1;
          client.stop();
          return; // on sort, pas de comm avec le server enphase
        }
        EnvoyDureeDerniereConnexionRenouveleeMs.add(millis() - nConnectStarted);

        TelnetPrintln("Connected to Envoy-S server HTTPS!");
      }


      unsigned long nRequestStarted = millis();

      bool bUseCookie = (EnvoySessionIdCookie != "");
      // Requete construite dans un buffer statique pour eviter les concat String sur le heap
      {
        static char s_reqLine[768];  // ~130 octets + token (~400-500) ; repli String au-delà
        int n;
        if (bUseCookie) {
          n = snprintf(s_reqLine, sizeof(s_reqLine), "GET %sCookie: sessionId=%s\r\n\r\n",
            baseRequest, EnvoySessionIdCookie.c_str());
        } else {
          EnvoyCompteTentativesRequeteAuthBearer += 1;
          n = snprintf(s_reqLine, sizeof(s_reqLine), "GET %sAuthorization: Bearer %s\r\n\r\n",
            baseRequest, TokenEnphase.c_str());
        }
        if (n > 0 && (size_t)n < sizeof(s_reqLine)) {
          client.println(s_reqLine);
        } else {
          // Token/cookie anormalement long : repli sur String plutot que d'envoyer une requete tronquee
          StockMessage(String("Envoy request line too long (") + n + "), String fallback");
          client.println(String("GET ") + baseRequest + (bUseCookie
            ? String("Cookie: sessionId=") + EnvoySessionIdCookie
            : String("Authorization: Bearer ") + TokenEnphase) + "\r\n\r\n");
        }
      }

      static char statusLine[256]; // 'static' pour en faire une variable globale et eviter de saturer la pile
      size_t statusLineLen = 0;
      int result = 0;
      do {
        // on consomme le buffer jusqu'à la premiere ligne d'une nouvelle reponse HTTP (contenant HTTP/), 
        // au cas où il restait du buffer non consommé de la precedente requete, recu entre-temps).
          result = ReadBufferUntilChar(client, statusLine, sizeof(statusLine), statusLineLen, '\n', TIMEOUT_WAITING_ANSWER);
      } while (!strstr(statusLine, "HTTP/") && result == READ_OK);
      // Cette ligne devrait alors contenir le HTTP response code (200, 401...)


      if(result != READ_OK || !strstr(statusLine, "HTTP/")) {
        if (result == READ_DISCONNECTED) {
          TelnetPrintln("Envoy connection closed before sending any HTTP response. Retrying new connection...");
        } else {
          StockMessage(String("Envoy error while reading HTTP response status, status= ") + ReadStatusToString(result) + ", partialLen=" + statusLineLen);
        }
        client.stop();
        continue;  //retry on next loop with a new connection
      }

      // si on a un 401 et qu'on a utilisé le cookie, alors on s'autorise 
      // à retenter avec le token via continue, apres avoir invalidé le cookie
      int httpCode = HttpStatusCode(statusLine);
      if(bUseCookie && (httpCode == 401 || httpCode == 403)) {
        EnvoySessionIdCookie = "";
	      StockMessage("Envoy SessionId not valid anymore, retrying with Auth Bearer...");
        continue;  //retry on next loop with auth bearer
      }

      if (!bUseCookie && (httpCode == 401 || httpCode == 403)) {
        // token refusé (expiré/révoqué) : on en redemande un, au plus toutes les TOKEN_RETRY_MS
        client.stop();
        if (EnphaseTokenRetryAllowed()) {
          StockMessage(String("Envoy rejected token (HTTP ") + httpCode + "), requesting a new one...");
          RenouvelleTokenEnphase();
        }
        return;  //retry on next LectureEnphase() call
      }

      if(httpCode != 200) {
        // toute autre erreur n'est pas récupérable, on sort de la fonction
        StockMessage(String("Envoy refused request: statusLine=[") + statusLine + "]");
        client.stop();
        return;  //retry on next LectureEnphase() call
      }

      // ici, on a un 200 OK, on va pouvoir sortir de la boucle et lire le JSON
      if(!bUseCookie) // si on a utilisé le token, on doit récupérer le cookie sessionId pour les prochains appels
      {
        EnvoyDureeDerniereRequeteViaAuthBearerMs.add(millis() - nRequestStarted);
      } else {
        EnvoyDureeDerniereRequeteViaSessionCookieMs.add(millis() - nRequestStarted);
      }
      if (CaptureEnvoySessionIdFromHeaders(client, TIMEOUT_WAITING_ANSWER)) {
        client.stop();
        return;
      }
      break;
    }

    unsigned long nJsonParsingStarted = millis();
    int nGlobalIndex = 0;
    int nPhaseIndex = 0;
    bool bMonoPhase = true;

    //TelnetPrintln("Waiting JSON data ...");

    // Plus gros bloc lu (en-tête compteur ou phase) : 643 octets sur la réponse réelle à 8 compteurs.
    static char jsonPayload[800]; // 'static' pour en faire une variable globale et eviter de saturer la pile
    size_t jsonPayloadLength = 0;

    // Saute L'entete d'ouverture de la trame JSON.
    int status = ReadBufferUntilChar(client, jsonPayload, sizeof(jsonPayload), jsonPayloadLength, '[', TIMEOUT_READ_PAYLOAD);

    if (status != READ_OK) {
      StockMessage(String("Envoy JSON Reading 1 failed, status= ") + ReadStatusToString(status) + ", partialLen=" + jsonPayloadLength);
      client.stop();
      return;
    }

    for (nGlobalIndex = 0; (nGlobalIndex < 8) && !bJsonLoadingFinished; nGlobalIndex++) {
      // Read Global Topic
      int status = ReadBufferUntilChar(client, jsonPayload, sizeof(jsonPayload), jsonPayloadLength, '[', TIMEOUT_READ_PAYLOAD);

      if (status != READ_OK) {
        StockMessage(String("Envoy JSON Reading 2 failed, status= ") + ReadStatusToString(status) + ", partialLen=" + jsonPayloadLength);
        client.stop();
        return;
      }
      delay(1);
      if (!EnvoyPayloadPrefixValide(jsonPayload, nGlobalIndex == 0 ? "" : "]},") ||
          !EnvoyPayloadValide(jsonPayload, true, !bMonoPhase,
                              nGlobalIndex == 1 && !bMonoPhase)) {
        StockMessage("Envoy invalid meter header");
        client.stop();
        return;
      }

      if (nGlobalIndex == 0) {
        //StockMessage(jsonPayload);
        float tension = ValJson("voltage", jsonPayload);
        // long eid = LongJson("eid", jsonPayload);

        //StockMessage("Tension Global0 ="+String(tension));

        if (tension > 280.0f)
          bMonoPhase = false;

        //StockMessage("bMonoPhase ="+String(bMonoPhase));

        if (!bMonoPhase) {
          PactProd = ValJson("activePower", jsonPayload);
          Tension_M = ValJson("voltage", jsonPayload);
          Intensite_M = ValJson("current", jsonPayload);
        }
      } else if (nGlobalIndex == 1) {
        energyObserver.header(jsonPayload);
        if (!bMonoPhase) {
            PactReseau = ValJson("activePower", jsonPayload);
          PactConso_M = PactReseau + PactProd;  // dans l'hypothese qu'il n'y a pas de l'énergie fournit par une batterie !
            PvaReseau = ValJson("apparentPower", jsonPayload);
            whDlvdCum = ValJson("actEnergyDlvd", jsonPayload);
            whRcvdCum = ValJson("actEnergyRcvd", jsonPayload);
            Frequence = ValJson("freq", jsonPayload);
        }
      }

      for (nPhaseIndex = 0; (nPhaseIndex < 3) && !bJsonLoadingFinished; nPhaseIndex++) {
        // Read Phase
        int status = ReadBufferUntilChar(client, jsonPayload, sizeof(jsonPayload), jsonPayloadLength, '}', TIMEOUT_READ_PAYLOAD);

        if (status != READ_OK) {
          StockMessage(String("Envoy JSON Reading 3 failed, status= ") + ReadStatusToString(status) + ", partialLen=" + jsonPayloadLength);
          client.stop();
          return;
        }
        if (jsonPayloadLength + 1 >= sizeof(jsonPayload)) {
          StockMessage("Envoy phase payload too long");
          client.stop();
          return;
        }
        jsonPayload[jsonPayloadLength++] = '}';
        jsonPayload[jsonPayloadLength] = '\0';
        if (!EnvoyPayloadPrefixValide(jsonPayload, nPhaseIndex == 0 ? "" : ",") ||
            !EnvoyPayloadValide(jsonPayload, false, nGlobalIndex == 0 && nPhaseIndex == 0,
                               nGlobalIndex == 1 && nPhaseIndex == 0 && bMonoPhase)) {
          StockMessage("Envoy invalid phase payload");
          client.stop();
          return;
        }
        delay(1);

        if ((nGlobalIndex == 0) && (nPhaseIndex == 0)) {
          if (bMonoPhase) {
            PactProd = ValJson("activePower", jsonPayload);
          }
        } else if ((nGlobalIndex == 1) && (nPhaseIndex == 0)) {
          Tension_M1 = ValJson("voltage", jsonPayload);
          Intensite_M1 = ValJson("current", jsonPayload);

          if (bMonoPhase) {
            //StockMessage(jsonPayload);
            energyObserver.phase(jsonPayload);
            PactReseau = ValJson("activePower", jsonPayload);
            PactConso_M = PactReseau + PactProd;  // dans l'hypothese qu'il n'y a pas de l'énergie fournit par une batterie !
            PvaReseau = ValJson("apparentPower", jsonPayload);
            whDlvdCum = ValJson("actEnergyDlvd", jsonPayload);
            whRcvdCum = ValJson("actEnergyRcvd", jsonPayload);
            Frequence = ValJson("freq", jsonPayload);
            
            Tension_M = Tension_M1;
            Intensite_M = Intensite_M1;
            //StockMessage("activePower="+String(PactReseau));
          }
        } else if ((nGlobalIndex == 1) && (nPhaseIndex == 1)) {
          Tension_M2 = ValJson("voltage", jsonPayload);
          Intensite_M2 = ValJson("current", jsonPayload);
        } else if ((nGlobalIndex == 1) && (nPhaseIndex == 2)) {
          Tension_M3 = ValJson("voltage", jsonPayload);
          Intensite_M3 = ValJson("current", jsonPayload);
          
          int tailStatus = ReadBufferUntilChar(client, jsonPayload, sizeof(jsonPayload),
                                               jsonPayloadLength, ']', TIMEOUT_READ_PAYLOAD);
          bool tailValid = tailStatus == READ_OK;
          for (size_t i = 0; i < jsonPayloadLength; ++i)
            tailValid = tailValid && isspace((unsigned char)jsonPayload[i]);
          if (tailValid) {
            // L'Envoy réel renvoie jusqu'à 8 compteurs : seule la fermeture du compteur 2 est validée.
            tailStatus = ReadBufferUntilChar(client, jsonPayload, sizeof(jsonPayload),
                                             jsonPayloadLength, '}', TIMEOUT_READ_PAYLOAD);
            tailValid = tailStatus == READ_OK;
            for (size_t i = 0; i < jsonPayloadLength; ++i)
              tailValid = tailValid && isspace((unsigned char)jsonPayload[i]);
            if (tailValid) {
              const int next = ReadNextNonSpaceChar(client, TIMEOUT_READ_PAYLOAD);
              tailValid = next == ']' || next == ',';
            }
          }
          if (!tailValid) {
            StockMessage("Envoy invalid or truncated JSON ending");
            client.stop();
            return;
          }
          bJsonLoadingFinished = true;

          unsigned long nNow = millis();
          EnvoyDureeDernierJsonParsingMs.add(nNow - nJsonParsingStarted);
          EnvoyDureeDerniereLectureCompleteMs.add(nNow - nLectureStarted);
          EnvoyCompteSuccesLectureComplete += 1;

          if(g_nLastGoodReading != 0)
            EnvoyIntervaleDernieresLecturesCompleteslMs.add(nNow - g_nLastGoodReading);          
          g_nLastGoodReading = nNow;


        }		
      }
    }
  }

  if (!bJsonLoadingFinished) {
	  //TelnetPrintln("JSON Loading failed");
    StockMessage("JSON Loading failed");
	  return;
  }
  
  PactReseau = PfloatMax(PactReseau);
  energyObserver.complete();
  if (PactReseau < 0) {
    PuissanceS_M_inst = 0;
    PuissanceI_M_inst = int(-PactReseau);
  } else {
    PuissanceI_M_inst = 0;
    PuissanceS_M_inst = int(PactReseau);
  }
  PvaReseau = PfloatMax(PvaReseau);
  if (PactReseau < 0) {
    PVAS_M_inst = 0;
    PVAI_M_inst = int(PvaReseau);
  } else {
    PVAI_M_inst = 0;
    PVAS_M_inst = int(PvaReseau);
  }
  Pva_valide = true;
  filtre_puissance();
  float PowerFactor = 0.0f;
  if ((PVA_M_moy) != 0) {
    PowerFactor = floor(100.0f * fabsf(Puissance_M_moy) / fabsf(PVA_M_moy)) / 100.0f;
    PowerFactor = min(PowerFactor, 1.0f);
  }
  PowerFactor_M = PowerFactor;

  if (whDlvdCum != 0) {
    if (LastwhDlvdCum == 0)
      LastwhDlvdCum = whDlvdCum;
    long DeltaWhSoutire = whDlvdCum - LastwhDlvdCum;
    LastwhDlvdCum = whDlvdCum;
    if (DeltaWhSoutire > 0) {
      Energie_M_Soutiree += DeltaWhSoutire;
    }
  }

  if (whRcvdCum != 0) {
    if (LastwhRcvdCum == 0)
      LastwhRcvdCum = whRcvdCum;
    long DeltaWhInjecte = whRcvdCum - LastwhRcvdCum;
    LastwhRcvdCum = whRcvdCum;
    if (DeltaWhInjecte > 0) {
      Energie_M_Injectee += DeltaWhInjecte;
    }
  }
  
  EnergieActiveValide = true;
  if (PactReseau != 0 || PvaReseau != 0) PuissanceRecue = true;  // Reset du Watchdog à chaque trame reçue de la passerelle Envoy-S metered
  if (cptLEDyellow > 30) cptLEDyellow = 4;
}

String PrefiltreJson(String F1, String F2, String Json) {
  int p = Json.indexOf(F1);
  Json = Json.substring(p);
  p = Json.indexOf(F2);
  Json = Json.substring(p);
  return Json;
}
String SubJson(String F1, String F2, String Json) {
  int p = Json.indexOf(F1);
  Json = Json.substring(p);
  p = Json.indexOf(F2);
  Json = Json.substring(0, p + 1);
  return Json;
}

float ValJson(String nom, String Json) {
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(",");
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  float val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    val = Json.toFloat();
  }
  return val;
}
double ValJson(const char* nom, const char* json)
// version alternative pour éviter les allocations de String et les copies de mémoire
{
  if (nom == nullptr || json == nullptr)
    return 0.0f;

  char key[64];

  int n = snprintf(key, sizeof(key), "\"%s\"", nom);
  if (n <= 0 || n >= (int)sizeof(key))
    return 0.0f;

  const char* p = strstr(json, key);
  if (p == nullptr)
    return 0.0f;

  p += n;

  while (*p && isspace((unsigned char)*p))
    p++;

  if (*p != ':')
    return 0.0f;

  p++;

  while (*p && isspace((unsigned char)*p))
    p++;

  char* endPtr = nullptr;
  double val = strtod(p, &endPtr);

  if (endPtr == p)
    return 0.0f;

  return val;
}
long LongJson(String nom, String Json) {  // Pour éviter des problèmes d'overflow
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(".");
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  long val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    val = Json.toInt();
  }
  return val;
}

long myLongJson(String nom, String Json) {  // Alternative a LongJson au dessus pour extraire chez RTE nb jour Tempo  https://particulier.RTE.fr/services/rest/referentiel/getNbTempoDays?TypeAlerte=TEMPO
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(",");       //<==== Recherche d'une virgule et non d'un point
  if (q == -1) q = Json.length();  //  /<==== Ajout de ces 2 lignes pour que la ligne p = min(p, q); ci dessous donne le bon résultat
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  long val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    val = Json.toInt();
  }
  return val;
}
unsigned long ULongJson(String nom, String Json) {  // Alternative a LongJson au dessus pour extraire chez RTE nb jour Tempo  https://particulier.RTE.fr/services/rest/referentiel/getNbTempoDays?TypeAlerte=TEMPO
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(",");       //<==== Recherche d'une virgule et non d'un point
  if (q == -1) q = Json.length();  //  /<==== Ajout de ces 2 lignes pour que la ligne p = min(p, q); ci dessous donne le bon résultat
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  unsigned long val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    Json = "0000" + Json;
    int L = Json.length();
    unsigned long y = (Json.substring(0, L - 5)).toInt();  //Problème des valeurs signées dans un unsigned
    unsigned long z = (Json.substring(L - 5)).toInt();
    val = (y * 100000) + z;
  }
  return val;
}
int IntJson(String nom, String Json) {  // Pour éviter des problèmes d'overflow
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(",");
  if (q == -1) q = Json.length();
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  int val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    val = Json.toInt();
  }
  return val;
}
byte ByteJson(String nom, String Json) {  // Pour éviter des problèmes d'overflow
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(",");
  if (q == -1) q = Json.length();
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  byte val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    val = Json.toInt();
  }
  return val;
}
unsigned short UShortJson(String nom, String Json) {  // Pour éviter des problèmes d'overflow
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(",");
  if (q == -1) q = Json.length();
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  unsigned short val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    val = Json.toInt();
  }
  return val;
}
short ShortJson(String nom, String Json) {  // Pour éviter des problèmes d'overflow
  int p = Json.indexOf(nom + "\":");
  Json = Json.substring(p);
  p = Json.indexOf(":");
  Json = Json.substring(p + 1);
  int q = Json.indexOf(",");
  if (q == -1) q = Json.length();
  p = Json.indexOf("}");
  if (p > 0)
    p = min(p, q);
  else
    p = q;
  short val = 0;
  if (p > 0) {
    Json = Json.substring(0, p);
    val = Json.toInt();
  }
  return val;
}


String StringJson(String nom, String Json) {
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
