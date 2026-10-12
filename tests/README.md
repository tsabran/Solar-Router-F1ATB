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
