# 🧪 Suite de tests et simulations Solar-Router-F1ATB

Batterie de tests automatisés et environnement de simulation pour le Solar Router F1ATB.

Les scripts `tests/<feature>/prepare-tests.sh` recoivent la racine du projet
et le dossier `tests/.build/`. Ils peuvent produire des fichiers d'inclusion
et emettre des dossiers d'inclusion supplementaires sur stdout (un par ligne).
Les diagnostics vont sur stderr et tout echec arrete le runner. Les dependances
firmware deja installees sont reutilisees sans telechargement automatique.
Les dossiers `stubs/` servent aux inclusions C++ ; les fichiers `test_*.js`
sont executes avec Node.js et recoivent le filtre de suite en premier argument.

## 📂 Structure

```text
tests/
├── harness/                        # Socle d'infrastructure commun
│   ├── mock_arduino.h              # Émulation légère des primitives ESP32 (millis, String, stream)
│   └── test_runner.h               # Mini-framework de test C++ sans dépendance
├── enphase/                        # Batterie de tests & simulation Enphase
│   ├── fixtures/                   # Traces JSON réelles Envoy-S Metered (mono, tri, import, export)
│   ├── mock_server/                # Mock server Python (Enlighten Cloud + Envoy-S)
│   │   └── mock_envoy.py
│   ├── router_enphase_adapter.h    # Adaptateur du code routeur sous test
│   ├── prepare-tests.sh           # Extraction filtre/statistiques/handlers firmware
│   ├── scripted_network.h         # Reponses TCP/TLS et horloge scriptables
│   ├── test_envoy_firmware.cpp     # Lecteur Enphase complet de production
│   ├── test_envoy_web.cpp          # Streaming HTTP et historique reel
│   ├── test_envoy_ui.js            # JavaScript embarque des donnees brutes
│   ├── test_enphase_parser.cpp     # Tests unitaires (JSON, calculs, compteurs, filtres, erreurs)
│   └── test_enphase_simulation.cpp # Simulation dynamique d'une journée complète (pilotage Triac)
├── main_tests.cpp                  # Point d'entrée du runner C++
└── run-tests.sh                    # Compilation C++, préparation et tests JavaScript
```

## 🚀 Lancer les tests

```bash
# Exécution complète de toute la suite
./tests/run-tests.sh

# Filtrer uniquement la suite Enphase
./tests/run-tests.sh Enphase

# Filtrer uniquement la simulation dynamique d'une journée
./tests/run-tests.sh Simulation

# Logique réelle du séquenceur (C++) et configuration web (JavaScript)
./tests/run-tests.sh Sequenceur
./tests/run-tests.sh SequenceurUI
```

Prérequis : Bash et un compilateur C++17 (`clang++` ou `g++`). Les tests
JavaScript du séquenceur utilisent Node.js et ses modules intégrés, sans npm.

## Metriques d'energie : calcul, firmware, MQTT et configuration

La branche `agents/energy-metrics-integration-rms` ajoute `tests/metrics/`.
Les deux anciens executables `energy_*_test.cpp` sont integres au runner
commun : chaque cas est enregistre et les assertions restent actives, meme
avec `NDEBUG`. Aucune installation automatique de dependances n'est faite.

Les tests utilisent la vraie bibliotheque ArduinoJson deja installee pour
le firmware. Si elle n'est pas dans le dossier Arduino habituel :

```bash
ARDUINOJSON_INCLUDE=/chemin/ArduinoJson/src ./tests/run-tests.sh
```

Les scripts de preparation peuvent ecrire des chemins d'inclusion additionnels
sur leur sortie standard ; les diagnostics doivent aller sur stderr.

Couverture des 34 cas C++ et 4 cas JavaScript propres aux metriques :

- `Metrics` : 15 cas de compteurs, limites de fraicheur, changements de signe,
  horodatages, parsing mono/triphase et simulation d'une heure confrontee a
  une integration de reference independante.
- `MetricsFirmware` : 12 cas incluant directement `EnergiePotentielle.ino` :
  configuration, forcages, interruptions, invalidation et reprise sans reset.
- `MetricsIntegration` : 4 cas dans `regulation_cases.h`, inclus par le banc
  sequenceur pour verifier ensemble le vrai regulateur et le suivi. Une
  comparaison avec/sans monitoring verifie l'identite des commandes,
  integrateurs et sorties ISR sur une simulation a mesures toutes les 2 s.
- `MetricsMQTT` : 3 cas compilant les fonctions de publication/discovery
  extraites du firmware : schemas JSON, topics, disponibilite, precision de
  six decimales, dimensions des messages et erreurs de publication explicites.
- `MetricsUI` : 4 cas executant le JavaScript embarque : opt-in, comportement
  par defaut, changement de mode et preservation apres reindexation.

```bash
./tests/run-tests.sh                  # 106 cas sur enphase-envoy-fix
./tests/run-tests.sh Metrics
./tests/run-tests.sh MetricsFirmware
./tests/run-tests.sh MetricsIntegration
./tests/run-tests.sh MetricsMQTT
./tests/run-tests.sh MetricsUI
```

Les tests MQTT interceptent les publications, sans broker ni Home Assistant.
Les fonctions de suivi sont reelles, mais les sections critiques ESP32 sont
des stubs : la concurrence entre coeurs n'est pas validee nativement.
Le lecteur reseau complet est maintenant teste avec un transport simule.
Le reseau physique, la persistance LittleFS et le timing electrique
restent a verifier sur materiel. Voir aussi les
[limites physiques des indicateurs](../docs/energie_potentielle.md).

## Séquenceur : tests du firmware réel

La branche `feature/mode-sequenceur` ajoute `tests/sequenceur/` :

- `prepare-tests.sh` extrait les fonctions actuelles `AppliquerRetard`,
  `RelaisSequenceValide`, `GestionOverproduction`, les modes et les tables
  Multi-Sinus du sketch dans `tests/.build/`. Une extraction incomplète échoue.
- `test_sequenceur.cpp` compile ces fonctions et le véritable `Actions.cpp`.
  Seuls le temps, le réseau et les sorties matérielles sont simulés.
- `test_sequenceur_ui.js` exécute les quatre blocs JavaScript de `JS_Actions.h`
  dans une VM Node.js, avec un DOM minimal et capture des requêtes de sauvegarde.

Les 19 cas C++ couvrent les demandes 0–100 %, les charges homogènes/hétérogènes,
les puissances par défaut, les groupes indépendants, la capacité maximale,
les forçages, la moyenne pondérée réelle, les modes de sortie, les conditions du
parent, PID/CACSI, les changements de configuration et les références invalides.
La simulation en boucle fermée appelle le vrai régulateur toutes les 200 ms
avec une mesure réseau renouvelée toutes les 2 s : après chaque palier de
production/consommation, l'écart à la cible reste inférieur à 1 W après 60 s.
La propriété staircase et la conservation de puissance sont vérifiées à chaque
itération, hors forçages qui peuvent volontairement rompre la staircase.

Les 8 cas JavaScript vérifient les valeurs par défaut, l'affichage, la sauvegarde
d'un séquenceur sans GPIO et la conservation des liens après suppression d'actions.

Limites : ces tests ne mesurent ni les harmoniques, ni le timing réel des ISR,
ni le comportement électrique des SSR. La sauvegarde HTTP est capturée, mais
la persistance complète sur ESP32 et la lecture réseau Enphase doivent encore
être vérifiées sur matériel ou dans leurs suites dédiées.

## ☀️ Ce qui est vérifié en mode Enphase

### Lecteur de production : branche `enphase-envoy-fix`

Le nouveau banc inclut directement `Source_EnphaseEnvoy.ino`, sans copie de
l'algorithme. Le filtre de `Stockage.ino`, les statistiques du sketch et les
handlers web selectionnes sont extraits automatiquement ; un renommage ou une
extraction incomplete fait echouer la preparation.

```bash
./tests/run-tests.sh EnvoyFirmware   # 25 cas C++
./tests/run-tests.sh EnvoyWeb        # 4 cas C++
./tests/run-tests.sh EnvoyUI         # 3 cas JavaScript
```

- Mono/triphase, import/export/zero, facteur de puissance, filtre RC reel et
  compteurs Wh, dont increments de 1 Wh au-dela de la precision d'un float.
- Reutilisation TLS, changement d'IP, cookie et bearer, fallback cookie 401/403,
  renouvellement apres refus, token absent, echec conserve l'ancien token,
  delai de 5 minutes et renouvellement a 30 jours.
- mDNS, fragments reseau, limites de buffers et requetes longues, timeout
  d'inactivite, deconnexion, codes HTTP exacts et entetes complets.
- JSON invalide/tronque, valeurs manquantes/non finies, JSON minifie et
  validation de la fin des tableaux avant de compter un succes.
- Watchdog applique aussi aux retours anticipes : apres plus de 10 s sans
  lecture valide, les puissances maison instantanees, filtrees et apparentes
  sont remises a zero ; les compteurs d'energie sont conserves. La reprise
  retrouve les mesures. Le delai de retry et le watchdog couvrent le wrap 32 bits.
- Scenario d'une heure : 1 800 lectures, variations solaires et appareil de
  2 kW, panne de 16 s puis reprise. Les commandes du regulateur reel restent
  couvertes separement par la simulation en boucle fermee du sequenceur.
- AJAX Enphase : 27 colonnes, statistiques, ratios/`NA`, absence de token et
  cookie dans la sortie. Historique chunked : 600 points circulaires, 4 sondes,
  actions actives et terminaison. JavaScript embarque : affichage des 27 champs,
  correspondance des colonnes, erreurs HTTP explicites et rafraichissement a 2 s.

Les tests ont motive les corrections du watchdog, des reponses HTTP, des
cookies expires, du renouvellement periodique, des compteurs Wh et du facteur
de puissance. La validation JSON utilise ArduinoJson deja present dans le
firmware, en conservant la lecture par petits objets plutot qu'un document
complet. Les reponses cloud sont bornees a 4 Ko / 5 s par endpoint pour eviter
qu'une connexion qui ne se ferme pas bloque indefiniment la mesure.

Validation ESP32 core 3.3.7 : binaire de 1 919 568 / 1 945 600 octets,
soit 98 % de la partition applicative et **26 032 octets disponibles**.
La marge reste faible : les prochaines features doivent surveiller cette limite.
Les 91 cas C++ passent aussi avec `NDEBUG`, AddressSanitizer et
UndefinedBehaviorSanitizer (verification des fuites indisponible sur macOS).

Limites : le transport scriptable n'execute pas lwIP/TLS, ni la concurrence
entre coeurs. Le mock HTTP verifie les corps transmis et la terminaison, pas
les octets TCP du framing chunked. Les couts CPU/heap sur ESP32, les vrais
serveurs Enphase, la persistence et le comportement electrique restent a
valider sur materiel. Tous les identifiants du banc sont fictifs ; aucun
contact avec Enlighten/Envoy reel ni flash n'est necessaire.

### Adaptateur initial et simulation de journee

Les suites `Enphase` et `Simulation` ci-dessous utilisent l'adaptateur initial :
elles ne remplacent pas les tests du lecteur de production ci-dessus.

1. **Parsing JSON robuste** :
   - Décodage des trames Envoy-S Metered v7 en monophasé et triphasé.
   - Gestion des clés de puissance (`activePower`), tension (`voltage`), intensité (`current`), fréquence (`freq`).
2. **Conventions de signe & Calculs** :
   - Surplus ($P_{reseau} < 0$) : calcul exact de la puissance injectée et nulle pour le soutirage.
   - Soutirage ($P_{reseau} \ge 0$) : calcul exact du soutirage et nulle pour l'injection.
   - Consommation globale du foyer : $P_{conso} = P_{reseau} + P_{prod}$.
3. **Compteurs d'énergie cumulés (Wh)** :
   - Calcul des deltas réels injectés et soutirés sur incrément des index Envoy.
4. **Filtre dynamique RC** :
   - Lissage des puissances avec et sans lissage long ($A=0.3$, $B=0.7$).
5. **Watchdog de sécurité (10 secondes)** :
   - Vérification de la coupure de sécurité et remise à zéro de toutes les puissances si l'Envoy devient injoignable ou envoie des trames corrompues pendant plus de 10 secondes.
6. **Simulation temporelle d'une journée complète** :
   - Suivi pas à pas de la courbe de production solaire, allumage d'appareils énergivores, passage nuageux et reprise après incident réseau, avec asservissement en boucle fermée du Triac.

## 🌐 Mock Server Python (Tests sur table / réseau)

Un serveur mock Python autonome simule l'ensemble des endpoints Enphase :

```bash
python3 tests/enphase/mock_server/mock_envoy.py --port 8080
```

Il expose :
- `POST /login/login.json` (Enlighten Cloud)
- `POST /tokens` (Entrez Cloud)
- `GET /ivp/meters/readings` (Passerelle Envoy locale)

## 🧩 Comment ajouter des tests pour une nouvelle feature

Pour ajouter les tests d'une autre feature (ex: `sequenceur`, `linky`, `shelly`) :
1. Créez un sous-dossier `tests/<nom_feature>/`.
2. Créez votre fichier `test_<nom_feature>.cpp`.
3. Utilisez la macro `TEST_CASE(NomSuite, NomTest) { ... ASSERT_EQ(...) }`.
4. `./tests/run-tests.sh` découvrira et exécutera automatiquement vos nouveaux tests !

Si nécessaire, un fichier `prepare-tests.sh` reçoit la racine du projet et le
dossier de build comme arguments ; il doit échouer explicitement si sa préparation
échoue. Les sous-dossiers `stubs/` sont ajoutés au chemin d'inclusion C++.
Les fichiers `test_*.js` sont également découverts ; ils reçoivent le filtre de
suite en premier argument et doivent retourner un code non nul en cas d'échec.
