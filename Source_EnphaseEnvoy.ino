//***********************************
//* Source EnPhase V7   			*
//***********************************

uint32_t ipToInt(IPAddress ip) {                                                                  //SR19
  return uint32_t(ip[0] << 24) | uint32_t(ip[1] << 16) | uint32_t(ip[2] << 8) | uint32_t(ip[3]);  //SR19
}

void Setup_Enphase() {

  //Résolution mDNS de http://envoy.local en adresse IP                                                                  //SR19
  //***************************************************                                                                  //SR19

  const char* host = "envoy";  //SR19
  IPAddress envoyIP;
  if (RMSextIPauto) {                                                                                //SR19
    if (!MDNS.begin(hostname)) {                                                                     //Init mDNS                                                                              //SR19
      TelnetPrintln("Erreur : impossible d'initialiser mDNS");                                       //SR19
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
    if (!clientSecu.connect(server1Enphase, 443, 3000))
      StockMessage("Connection failed to Enlighten server :" + Host);
    else {
      TelnetPrintln("Connected to Enlighten server:" + Host);
	    StockMessage("Connected to Enlighten server :" + Host);
      clientSecu.println("POST " + adrEnphase + "?" + requestBody + " HTTP/1.0");
      clientSecu.println("Host: " + Host);
      clientSecu.println("Connection: close");
      clientSecu.println();
      String line = "";
      while (clientSecu.connected()) {
        line = clientSecu.readStringUntil('\n');
        if (line == "\r") {
          TelnetPrintln("headers 1 Enlighten received");
          JsonToken = "";
        }

        JsonToken += line;
      }
      // if there are incoming bytes available
      // from the server, read them and print them:
      while (clientSecu.available()) {
        char c = clientSecu.read();
        Serial.write(c);
      }
      clientSecu.stop();
    }
    Session_id = StringJson("session_id", JsonToken);
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
    if (!clientSecu.connect(server2Enphase, 443, 3000))
      StockMessage("Connection failed to :" + Host);
    else {
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
      String line = "";
      JsonToken = "";
      while (clientSecu.connected()) {
        line = clientSecu.readStringUntil('\n');
        if (line == "\r") {
          TelnetPrintln("headers 2 enlighten received");
          JsonToken = "";
        }

        JsonToken += line;
      }
      // if there are incoming bytes available
      // from the server, read them and print them:
      while (clientSecu.available()) {
        char c = clientSecu.read();
        Serial.write(c);
      }
      clientSecu.stop();
      JsonToken.trim();
      TelnetPrintln("Token :" + JsonToken);
      //StockMessage("Token :" + JsonToken);
      if (JsonToken.length() > 50) {
        TokenEnphase = JsonToken;
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
  if (maxSize == 0) return READ_INVALID_ARGUMENT;

  outLen = 0;
  out[0] = '\0';

  const size_t maxLen = maxSize - 1;
  unsigned long lastActivity = millis();

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

    if ((unsigned long)(millis() - lastActivity) >= timeoutMs) {
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




String ExtractEnvoySessionId(const String& httpHeader)
{
  int p = httpHeader.indexOf("sessionId=");
  if (p < 0)
    return "";

  p += 10; // strlen("sessionId=")

  int q = httpHeader.indexOf(';', p);
  if (q < 0)
    q = httpHeader.length();

  return httpHeader.substring(p, q);
}



bool CaptureEnvoySessionIdFromHeaders(NetworkClientSecure& pClient, unsigned long nTimeout)
{

  static char httpHeaderBuf[256]; // 'static' pour en faire une variable globale et eviter de saturer la pile
  size_t httpHeaderLength = 0;

  // Seuls les appels locaux Envoy authentifiés par bearer nécessitent la lecture des headers,
  // car ils renvoient le cookie local `sessionId` à réutiliser sur les appels suivants.
  while(true)
  {
    int status = ReadBufferUntilChar(pClient, httpHeaderBuf, sizeof(httpHeaderBuf), httpHeaderLength, '\n', nTimeout);

    if (status != READ_OK) {
      StockMessage(String("Envoy Headers reading failed, status= ") + ReadStatusToString(status) + ", partialLen=" + httpHeaderLength);
      return true;
    }

    String httpHeader = String(httpHeaderBuf, httpHeaderLength);
    httpHeader.trim();
    if (httpHeader.length() == 0)
      return false; //all headers have been read

    if(httpHeader.startsWith("Set-Cookie:"))
    {
      String newSessionId = ExtractEnvoySessionId(httpHeader);
      if(newSessionId != "")
      {
        EnvoySessionIdCookie = newSessionId;
	      StockMessage("Envoy local sessionId successfully cached");
      }
    }
  }
}

void LectureEnphase() 
{
  #define TIMEOUT_READ_PAYLOAD    100
  #define TIMEOUT_WAITING_ANSWER  500
  #define TIMEOUT_CONNECT         3000

  EnvoyCompteTentativesLectureComplete += 1;
  // init variable 
  static unsigned long g_nLastGoodReading = millis();
  bool bJsonLoadingFinished = false;

  float PactReseau = 0.0f;
  float PvaReseau = 0.0f;
  long whDlvdCum = 0L;  // on perd les decimals après la virgule avec un type long
  long whRcvdCum = 0L; 
  
  String host = IP2String(RMSextIP);
  String baseRequest;
  baseRequest = "/ivp/meters/readings HTTP/1.1\r\nHost: " + host + "\r\nAccept: application/json\r\nConnection: keep-alive\r\n";
  static NetworkClientSecure client; 

  constexpr uint32_t TOKEN_REFRESH_MS = 30UL * 24UL * 60UL * 60UL * 1000UL;
  if (TokenEnphase.length() > 50 && EnphaseUser != "") 
  {  
    // Connexion pour firmware V7 en https
    
    if ((millis() - lastTokenUpdate) > TOKEN_REFRESH_MS) 
	  {    // Tout les 30 jours on recherche un nouveau Token
      lastTokenUpdate = millis();                         // overflow compatible!
      Setup_Enphase();
    }

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
        if (!client.connect(host.c_str(), 443)) 
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
      if(bUseCookie) {
        client.println("GET " + baseRequest + "Cookie: sessionId=" + EnvoySessionIdCookie + "\r\n\r\n");
      } else {
        EnvoyCompteTentativesRequeteAuthBearer += 1;
        client.println("GET " + baseRequest + "Authorization: Bearer " + TokenEnphase + "\r\n\r\n");
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


      if(!strstr(statusLine, "HTTP/")) {
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
      if(bUseCookie && strstr(statusLine, "401")) {
        EnvoySessionIdCookie = "";
	      StockMessage("Envoy SessionId not valid anymore, retrying with Auth Bearer...");
        continue;  //retry on next loop with auth bearer
      }

      if(!strstr(statusLine, "200")) {
        // toute autre erreur n'est pas récupérable, on sort de la fonction
        StockMessage(String("Envoy refused request: statusLine=[") + statusLine + "]");
        client.stop();
        return;  //retry on next LectureEnphase() call
      }

      // ici, on a un 200 OK, on va pouvoir sortir de la boucle et lire le JSON
      if(!bUseCookie) // si on a utilisé le token, on doit récupérer le cookie sessionId pour les prochains appels
      {
        EnvoyDureeDerniereRequeteViaAuthBearerMs.add(millis() - nRequestStarted);
        CaptureEnvoySessionIdFromHeaders(client, TIMEOUT_WAITING_ANSWER);
      } else {
        EnvoyDureeDerniereRequeteViaSessionCookieMs.add(millis() - nRequestStarted);
      }
      break;
    }

    unsigned long nJsonParsingStarted = millis();
    int nGlobalIndex = 0;
    int nPhaseIndex = 0;
    bool bMonoPhase = true;

    //TelnetPrintln("Waiting JSON data ...");

    static char jsonPayload[1024]; // 'static' pour en faire une variable globale et eviter de saturer la pile
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
        jsonPayload[jsonPayloadLength-1] = '}';
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
    //Protection contre les mauvaises lectures qui perdureraient plus de 10s !!!
    if ((millis() - g_nLastGoodReading) > 10000) {
      PactProd = 0.0f;
      PactConso_M = 0;
      PactReseau = 0.0f;
      PactConso_M = 0.0f;
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
	  //TelnetPrintln("JSON Loading failed");
    StockMessage("JSON Loading failed");
	  return;
  }
  
  PactReseau = PfloatMax(PactReseau);
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
    PowerFactor = floor(100.0f * fabsf(Puissance_M_moy) / PVA_M_moy) / 100.0f;
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
float ValJson(const char* nom, const char* json)
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
  float val = strtof(p, &endPtr);

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
