# Build, traçabilité et déploiement

Outillage en ligne de commande (macOS/Linux) pour compiler le firmware avec `arduino-cli`,
assembler des branches dans un clone temporaire, archiver chaque binaire avec un manifeste
et le flasher par USB ou par OTA.

| Fichier | Rôle |
|---------|------|
| `merge-and-build.sh` | Clone temporaire, merges, compilation, archivage, `refs/builds/<tag>` |
| `build-binary.sh` | Compilation d'un répertoire de sources et archivage dans `releases/` |
| `flash.sh` | Flash série (esptool) ou OTA web (`/update`) d'une release archivée |
| `merge-branches.conf` | Liste ordonnée des branches à merger |
| `sketch.yaml` | Profil arduino-cli : plateforme et bibliothèques figées |
| `tools/build-lib.sh`, `tools/write-manifest.py` | Fonctions communes, génération du manifeste |
| `partitions.csv` | Table de partitions (« Partition Scheme → Custom » dans l'IDE) |

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
./build-binary.sh                       # sources du répertoire courant, profil esp32
BUILD_TAG=test ./build-binary.sh        # tag affiché dans la version
./build-binary.sh --no-profile          # core/bibliothèques globaux et $FQBN
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

Les branches de `merge-branches.conf` (ou passées en arguments, qui remplacent le fichier)
sont ensuite mergées dans l'ordre, en `--no-ff`, par SHA résolu au démarrage.

```bash
./merge-and-build.sh --list-branches              # branches locales, sélectionnées ou non
./merge-and-build.sh --dry-run                    # teste les merges sans compiler
./merge-and-build.sh                              # localdev + config
./merge-and-build.sh --base enphase-envoy-fix build-automation ota-improve
./merge-and-build.sh --localdev --localrelease    # deux builds
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

Tout est écrit sous la racine du worktree principal (`SR_OUTPUT_ROOT`), ignorée par git :

```
releases/Solar_Router_V17_25_localdev-20261012-163206/
  Solar_Router_V17_25_localdev-20261012-163206.ino.bin      # app (flash/OTA)
  ….ino.elf, ….ino.bootloader.bin, ….ino.partitions.bin, ….ino.merged.bin
  manifest.txt          # date, tag, base, merges, toolchain, bibliothèques, SHA-256
  worktree.patch        # modifications locales incluses (localdev)
  build.log             # sortie arduino-cli
  merge-and-build.log   # merges + compilation
  SHA256SUMS
logs/merge-and-build_<timestamp>.log
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
./flash.sh --serial --dry-run                    # affiche la commande esptool
./flash.sh --serial /dev/cu.usbserial-0001       # flash USB
./flash.sh --ota 192.168.1.50 Solar_Router_V17_25_localdev-20261012-163206
```

- **Série** : écrit bootloader (0x1000), partitions (0x8000), `boot_app0` (0xE000) et app
  (0x10000), comme l'IDE. NVS (WiFi) et LittleFS (configuration du routeur) sont conservés.
  Ne pas flasher `….merged.bin` à 0x0 : cette image de 4 Mo écrase toute la flash, configuration comprise.
- **OTA** : envoie l'app sur `http://<ip>/update` (page « Mise à jour » du routeur), attend
  le redémarrage puis vérifie que `/ParaFixe` renvoie une version contenant le `BUILD_TAG`.
