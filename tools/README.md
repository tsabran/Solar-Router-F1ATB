# Build, traçabilité et déploiement

Outillage en ligne de commande (macOS/Linux) pour compiler le firmware avec `arduino-cli`,
assembler des branches dans un clone temporaire, archiver chaque binaire avec un manifeste
et le flasher par USB ou par OTA.

Tout l'outillage est dans `tools/` ; les commandes ci-dessous se lancent depuis la racine
du sketch (elles fonctionnent aussi depuis n'importe quel répertoire du dépôt).

| Fichier | Rôle |
|---------|------|
| `tools/merge-and-build.sh` | Clone temporaire, merges, compilation, archivage, `refs/builds/<tag>` |
| `tools/build-binary.sh` | Compilation d'un répertoire de sources et archivage dans `releases/` |
| `tools/flash.sh` | Flash série (esptool) ou OTA web (`/update`) d'une release archivée |
| `tools/merge-branches.conf` | Liste ordonnée des branches à merger |
| `tools/flash.conf.example` | Modèle de configuration locale de `flash.sh` |
| `tools/lib/` | Fonctions communes (`build-lib.sh`), génération du manifeste (`write-manifest.py`) |
| `sketch.yaml` | Profil arduino-cli : plateforme et bibliothèques figées (reste à la racine du sketch) |
| `partitions.csv` | Table de partitions (« Partition Scheme → Custom » dans l'IDE) |

Fichiers locaux, non versionnés, à la racine du worktree principal : `releases/` (binaires,
manifestes, logs, `DEPLOYMENTS.log`), `flash.conf` (cibles de flash) et `.tmp-build/`
(répertoire de travail temporaire).

## Installation de la toolchain

```bash
brew install arduino-cli            # Linux : https://arduino.github.io/arduino-cli/latest/installation/
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
# Les archives ESP32 (~500 Mo) viennent de release-assets.githubusercontent.com : le délai
# par défaut (60 s) est souvent dépassé.
arduino-cli config set network.connection_timeout 600s
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.7
```

Avec le profil `sketch.yaml` (comportement par défaut de `build-binary.sh`), arduino-cli
installe lui-même la plateforme et les bibliothèques aux versions figées, dans une copie
isolée (`~/Library/Arduino15/internal/`). L'installation globale des bibliothèques n'est
utile que pour l'IDE ou `--no-profile` :

```bash
arduino-cli lib install "ArduinoJson@7.4.3" "DallasTemperature@4.0.6" "EthernetESP32@1.0.2" \
  "LovyanGFX@1.2.19" "PubSubClient@2.8" "UrlEncode@1.0.1"
```

Si un téléchargement échoue malgré tout, récupérer l'archive avec `curl -L -o` à l'URL
affichée par arduino-cli, dans `~/Library/Arduino15/staging/packages/` (Linux :
`~/.arduino15/staging/packages/`), puis relancer : arduino-cli réutilise l'archive si sa
taille et son checksum correspondent à l'index.

Mise à jour des versions : modifier `sketch.yaml` (et la liste ci-dessus) dans un commit dédié.

## Compiler

```bash
tools/build-binary.sh                       # sources du répertoire courant, profil esp32
BUILD_TAG=test tools/build-binary.sh        # tag affiché dans la version
tools/build-binary.sh --no-profile          # core/bibliothèques globaux et $FQBN
```

FQBN : `esp32:esp32:esp32:PartitionScheme=custom`. Le script vérifie que le binaire tient
dans la partition app de `partitions.csv` (1900K) et avertit au-delà de 95 %.

La version du firmware devient `17.25 (<BUILD_TAG> <sha>)`, par exemple
`17.25 (localdev-20261012-163206 ee095c0)`. Le SHA est suffixé par `-dirty` si des
modifications locales ont été incluses. Elle apparaît au boot sur le port série, en Telnet,
dans l'interface web et dans le message MQTT `sw`.

## Assembler des branches : merge-and-build.sh

Le script travaille dans un clone local temporaire (`.tmp-build/merge-<tag>/clone`) : il ne
modifie ni les branches ni le working tree.

| Base | Option | Contenu |
|------|--------|---------|
| localdev *(défaut)* | `--localdev` | HEAD du répertoire de travail + modifications non commitées (indexées, non indexées, fichiers non suivis non ignorés) |
| localrelease | `--localrelease` | `origin/main` après `git fetch origin main` |
| quelconque | `--base REF` | branche, tag ou commit, sans modifications locales |

Les branches de `tools/merge-branches.conf` (ou passées en arguments, qui remplacent le fichier)
sont ensuite mergées dans l'ordre, en `--no-ff`, par SHA résolu au démarrage.

```bash
tools/merge-and-build.sh --list-branches              # branches locales, sélectionnées ou non
tools/merge-and-build.sh --dry-run                    # teste les merges sans compiler
tools/merge-and-build.sh                              # localdev + config
tools/merge-and-build.sh --base enphase-envoy-fix build-automation ota-improve
tools/merge-and-build.sh --localdev --localrelease    # deux builds
```

En cas de conflit, le script liste les fichiers en conflit, annule le merge et s'arrête
(`--dry-run` continue pour signaler tous les conflits). Résoudre le conflit sur la branche
source, ou retirer la branche de la liste, puis relancer.

### Depuis une branche qui n'hérite pas de build-automation

L'outillage est pris dans le répertoire du script, les sources dans le répertoire courant
(ou `--workdir`). Garder un worktree `build-automation` et l'appeler depuis n'importe quel
worktree :

```bash
alias srbuild="$HOME/dev/private/Solar-Router-F1ATB.worktrees/esp32-solar-router-build-automation/merge-and-build.sh"
cd ../enphase-envoy-fix
srbuild build-automation feature/mode-sequenceur ota-improve
```

Les fichiers de build (`sketch.yaml`, `partitions.csv`) viennent alors des sources
assemblées : inclure `build-automation` dans la liste si la base ne les contient pas.

### Sécurité

Les fichiers secrets (`.enphase_token_cache.json`, `*.token`, `.env`…) ne sont jamais copiés
dans le clone, le staging ni le patch archivé, même s'ils ne sont pas ignorés par git.

## Traçabilité

Les releases sont écrites sous la racine du worktree principal (`SR_OUTPUT_ROOT`), dans
`releases/`, ignoré par git. Les fichiers de travail (clone, copie des sources, compilation,
logs en cours) vont dans `.tmp-build/` et sont supprimés en fin de run ; après un échec,
la sortie console contient tout le diagnostic.

```
releases/Solar_Router_V17_25_localdev-20261012-163206/
  Solar_Router_V17_25_localdev-20261012-163206.ino.bin      # firmware (OTA / flash)
  Solar_Router_V17_25_localdev-20261012-163206.ino.elf.gz   # symboles, pour décoder un crash
  manifest.txt          # date, tag, base, merges, toolchain, bibliothèques, SHA-256
  worktree.patch        # modifications locales incluses (localdev)
  build.log             # sortie arduino-cli
  merge-and-build.log   # merges + compilation
  SHA256SUMS
  flash-<date>-<mode>-<cible>.log   # un log par déploiement (flash.sh)
releases/DEPLOYMENTS.log            # historique : une ligne par déploiement
```

Le manifeste contient la date, `BUILD_TAG`, le SHA de la base, la liste ordonnée des
branches mergées avec leurs SHA, le résumé du patch, les versions d'arduino-cli, de la
plateforme et des bibliothèques, le FQBN, le profil, les tailles et le SHA-256 des fichiers.

L'arbre exact compilé est conservé dans le dépôt sous `refs/builds/<tag>` :

```bash
git for-each-ref refs/builds
git log --oneline --graph refs/builds/localdev-20261012-163206
git switch --detach refs/builds/localdev-20261012-163206
git update-ref -d refs/builds/<tag>                     # supprimer une ref de build
```

### Date « Software Info »

`Software Info: <date>` vient de `__DATE__`/`__TIME__` dans le core ESP32
(`chip-debug-report.cpp`). Sans `--build-path`, arduino-cli réutilise le `core.a` en cache
(`~/Library/Caches/arduino/cores/…`) : la date affichée est celle de la première compilation
du core, pas celle du firmware (d'où « Mar 18 2026 » sur un build du 29/06). `build-binary.sh`
utilise un `--build-path` neuf à chaque build, la date est donc juste, mais c'est
`BUILD_TAG`/SHA dans la version qui fait foi.

## Déployer : flash.sh

Sans argument de release, la plus récente de `releases/` est utilisée. Le SHA-256 est vérifié
contre `SHA256SUMS` et une confirmation est demandée (`--yes` pour l'omettre).

```bash
tools/flash.sh --serial --dry-run                    # affiche la commande esptool
tools/flash.sh --serial /dev/cu.usbserial-0001       # flash USB
tools/flash.sh --ota 192.168.1.50 Solar_Router_V17_25_localdev-20261012-163206
tools/flash.sh --ota                                 # cible = OTA_HOST de flash.conf
```

**Configuration** : `flash.conf` à la racine du worktree principal (ignoré par git, modèle
`tools/flash.conf.example`, autre chemin via `SR_FLASH_CONF`) fournit les cibles par défaut :
`OTA_HOST=192.168.1.50` et, optionnel, `SERIAL_PORT=/dev/cu.usbserial-0001`. Le fichier
est lu (pas sourcé) ; une cible passée en argument est prioritaire.

**Historique** : chaque déploiement réel (hors `--dry-run`, et seulement une fois confirmé)
écrit la sortie complète dans `<release>/flash-<date>-<mode>-<cible>.log` et ajoute une ligne
tabulée à `releases/DEPLOYMENTS.log` :

```
<date ISO>  OK|FAIL  ota|serial  <cible>  before=<version>  after=<version>  release=…  sha256=…  log=…
```

```bash
column -ts$'\t' releases/DEPLOYMENTS.log        # quel firmware tourne où, depuis quand
```

- **Série** : écrit `boot_app0` (0xE000, démarrage sur app0) et l'app (0x10000). NVS (WiFi)
  et LittleFS (configuration du routeur) sont conservés. La carte doit déjà avoir la table
  de partitions Custom (tout routeur en version ≥ 14.02) ; pour une carte vierge, faire un
  premier téléversement depuis l'IDE Arduino (Partition Scheme → Custom).
- **OTA** : envoie l'app sur `http://<ip>/update` (page « Mise à jour » du routeur), attend
  le redémarrage puis vérifie que `/ParaFixe` renvoie une version contenant le `BUILD_TAG`.
