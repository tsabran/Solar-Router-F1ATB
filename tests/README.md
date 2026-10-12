# 🧪 Suite de tests et simulations Solar-Router-F1ATB

Batterie de tests automatisés et environnement de simulation pour le Solar Router F1ATB.

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
└── run-tests.sh                    # Script de compilation et d'exécution rapide (< 1s)
```

## 🚀 Lancer les tests

```bash
# Exécution complète de toute la suite
./tests/run-tests.sh

# Filtrer uniquement la suite Enphase
./tests/run-tests.sh Enphase

# Filtrer uniquement la simulation dynamique d'une journée
./tests/run-tests.sh Simulation
```

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
