# Build, traçabilité et déploiement

Outillage en ligne de commande (macOS/Linux) pour compiler le firmware avec `arduino-cli`,
assembler des branches dans un clone temporaire, archiver chaque binaire avec un manifeste
et le flasher par USB ou par OTA.

Tout l'outillage est dans `tools/` ; les commandes ci-dessous se lancent depuis la racine
du sketch (elles fonctionnent aussi depuis n'importe quel répertoire du dépôt).

| Fichier | Rôle |
|---------|------|
| `tools/merge-and-build.sh` | Clone temporaire, merges, compilation, archivage, `refs/builds/<label>`, tags de release |
| `tools/build-binary.sh` | Compilation d'un répertoire de sources et archivage dans `releases/` |
| `tools/flash.sh` | Flash série (esptool) ou OTA web (`/update`) d'une release archivée |
| `tools/flash.conf.example` | Modèle de configuration locale de `flash.sh` |
| `tools/build.conf.example` | Modèle de configuration locale de `merge-and-build.sh` (`MERGE_BRANCHES`, `RELEASE_SUFFIX`) |
| `tools/lib/` | Fonctions communes (`build-lib.sh`), génération du manifeste (`write-manifest.py`) |
| `sketch.yaml` | Profil arduino-cli : plateforme et bibliothèques figées (reste à la racine du sketch) |
| `partitions.csv` | Table de partitions (« Partition Scheme → Custom » dans l'IDE) |

Fichiers locaux, non versionnés, à la racine du worktree principal : `releases/` (binaires,
manifestes, logs, `DEPLOYMENTS.log`), `build.conf` (branches à merger,
suffixe des releases), `flash.conf` (cibles de flash) et `.tmp-build/` (répertoire de travail temporaire).
Si la branche du worktree principal n'a pas le `.gitignore` de `build-automation`, les
ajouter à `.git/info/exclude` : sinon ils comptent comme modifications locales (label `-wip`).

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
BUILD_TAG=test tools/build-binary.sh        # label affiché dans la version, utilisé tel quel
tools/build-binary.sh --no-profile          # core/bibliothèques globaux et $FQBN
tools/build-binary.sh --no-gzip             # minification seule, sans compression
tools/build-binary.sh --no-minify --no-gzip # corps originaux, pour comparaison
BUILD_TAG=diag tools/build-binary.sh --define DIAG_HEAP  # build de mesure
```

`--define NOM[=VALEUR]` (répétable) ajoute un `#define` à toute la compilation :

- `DIAG_HEAP` : échantillonneur du tas et endpoint `/ajax_diag` (minimum du tas
  depuis la dernière lecture, minima par minute sur 60 min, marges de pile).
  L'endpoint n'est pas authentifié : réservé aux builds de mesure, ne pas
  l'activer dans un firmware de production.
- `HTTP_DEBUG_LOG` : logs heap/durée des handlers web.

### Ressources web embarquées

Installer une fois les deux petits minifieurs Python (pas de Node/npm au build) :

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements-assets.txt
# Facultatif : relire .envrc, puis direnv allow pour les shells interactifs.
```

`build-binary.sh` utilise la `.venv` du checkout de l'outillage, sinon `python3`.
`ASSET_PYTHON=/chemin/python` permet de choisir explicitement l'interpréteur.
Aucune installation automatique ; une dépendance manquante arrête le build.

`prepare-assets.py` transforme seulement la copie de staging, jamais les sources :

- minification JS/CSS par `rjsmin`/`rcssmin` aux versions figées ; pas de renommage
  ni d'optimisation des expressions ;
- dans le HTML, seuls les blocs `<script>`/`<style>` sont minifiés. Les espaces
  du balisage, les attributs, les textes et l'ordre des scripts restent inchangés ;
- suffixe `?v=<SHA256 tronqué à 20 caractères>` pour les URLs JS/CSS/icônes/manifest
  locales, y compris les icônes dans le manifest. Le hash couvre toutes les
  ressources préparées avant l'ajout des suffixes et le mode gzip/non-gzip,
  indépendamment de `BUILD_TAG` ;
- `Cache-Control: public, max-age=31536000, immutable` uniquement si `v` correspond
  au contenu du firmware courant. URL sans version ou ancienne : `no-store` ;
- HTML, `/RuntimeJS` (`biSonde`), `/runtime.css` (couleurs), `/PinsActionsJS` :
  `no-store`. Le CSS dynamique précède le CSS statique sur toutes les pages ;
  le JS dynamique précède `MainJS1` sur l'accueil ;
- gzip des 27 ressources HTTP embarquées, par défaut, après insertion des URLs
  versionnées. Seul le gzip est stocké en flash, sans doublon brut. Le tableau
  de couleurs par défaut, utilisé par le firmware, n'est pas compressé ;
- `--no-gzip` désactive la compression ; `--no-minify` désactive indépendamment
  la minification. Les deux conservent le versionnement/cache et l'extraction
  des valeurs dynamiques.

Sans ce build (IDE Arduino), le tag vide désactive le cache immutable. L'IDE
compile directement les sources lisibles, sans dépendance Python.
Les anciennes branches sans `StaticAssetCache.h` restent compilables sans génération.

Chaque release générée archive `assets.json` dans les checksums : `original` et
`prepared` sont les tailles UTF-8 avant suffixes ; `versioned` inclut les suffixes ;
`stored` est la longueur réellement servie, gzip ou non, selon `encoding`.
`gzip` est la taille compressée du corps versionné (une estimation seulement si
`encoding` vaut `identity`). Le `.bin` fait foi pour la flash réellement utilisée :
ces tailles excluent le terminateur C, l'alignement, le code et les endpoints dynamiques.

#### Résultat de compilation initial (14 octobre 2026)

Sur cette branche, profil ESP32 3.3.7 identique, après extraction des valeurs dynamiques :

| Variante | Binaire app | Libre sur partition app (1 945 600 o) |
|----------|------------:|-------------------------------------:|
| Sans minification (`assets-raw-20261014a`) | 1 921 472 o | 24 128 o |
| Minifiée (`assets-min-20261014a`) | 1 848 608 o | 96 992 o |

Gain mesuré sur le binaire : **72 864 o (71,16 Kio)**. RAM globale statique
identique : 93 736 o. Ce n'est **pas** une mesure du creux de tas en service.
Les corps embarqués passent de 242 859 à 169 998 o avant suffixes de version.

Avec le serveur gzip et ses métadonnées, les deux builds comparatifs suivants
utilisent les mêmes sources et le même profil :

| Variante | Binaire app | Libre sur partition app |
|----------|------------:|------------------------:|
| Minifiée, `--no-gzip` (`assets-plain-20261014b`) | 1 851 056 o | 94 544 o |
| Minifiée + gzip, défaut (`assets-gzip-20261014b`) | 1 734 256 o | 211 344 o |

Gain gzip isolé : **116 800 o (114,06 Kio)**. Le gain total par rapport au premier
build sans minification est de **187 216 o (182,83 Kio)**, incluant l'évolution du
code serveur. RAM globale statique toujours identique : 93 736 o. Aucun de ces
chiffres ne mesure le tas en service.

Le build final déployé `assets-gzip-20261014c`, après centralisation du cache
pour éviter deux en-têtes contradictoires sur les erreurs 406, fait **1 733 824 o**.
Il laisse **211 776 o** dans la partition app ; gain total face au premier build
non minifié : **187 648 o (183,25 Kio)**. Son tag de ressources reste identique à
celui du build précédent, car les corps servis sont inchangés.

| Ressource | Original | Minifié | gzip du minifié (estimation) |
|-----------|---------:|--------:|----------------------------:|
| MainJS1 | 12 211 o | 5 997 o | 2 010 o |
| MainJS2 | 18 126 o | 13 367 o | 3 622 o |
| MainJS3 | 15 317 o | 11 147 o | 3 106 o |
| ParaCommunJS | 5 308 o | 3 654 o | 1 527 o |
| CommunCSS | 1 548 o | 1 476 o | 608 o |

#### gzip : activé par défaut au build

La compression **au build**, puis l'envoi des octets compressés avec leur longueur
binaire explicite et `Content-Encoding: gzip`, est viable : les navigateurs modernes
décompressent nativement, sans compression ni décompression sur l'ESP32. Le CPU du
routeur transfère seulement moins d'octets. `GeneratedStaticAssets.h`, produit
uniquement en staging, fournit les longueurs et encodages ; `SendStaticAsset`
n'utilise pas `strlen` sur les gzip (octets NUL). Métadonnée absente : erreur
HTTP 500 journalisée, jamais un envoi tronqué.

Le serveur collecte `Accept-Encoding` et envoie `Vary: Accept-Encoding`. Il accepte
`gzip` ou `*` de qualité positive, sans distinction de casse ; un `gzip;q=0`
explicite prend priorité sur `*`. Un champ vide ou ne permettant pas gzip produit
une erreur HTTP 406 journalisée et `no-store`, sans `Content-Encoding`.
L'absence du champ permet gzip conformément à HTTP. Les clients sans décompression
automatique doivent donc annoncer `identity`, ou utiliser par exemple `curl --compressed`.
Il n'y a pas de variante brute de secours afin de préserver le gain flash.

Les corps stockés après minification, versionnement et gzip totalisent 53 936 o
(y compris 180 o de couleurs par défaut non compressées). Les corps dynamiques
restent non compressés et non cachés ; on ne peut pas préfixer un gzip avec du
JS/CSS non compressé. Les sources IDE restent lisibles et non compressées.

Le cache chaud évitera les gros transferts ; le test `cache:'no-store'` mesure
au contraire le chargement à froid. `send_P`/`write_P` ne copient pas nécessairement
toute la ressource sur le tas ESP32 : la réduction des buffers TCP et donc du
creux n'est **pas garantie** par la seule réduction de taille.

#### Mesure sur le routeur de production

Le diagnostic firmware n'est pas inclus dans cette branche. Pour un build de
mesure, fusionner `diag/heap-sampler` uniquement dans le répertoire temporaire
d'assemblage, puis compiler ce répertoire avec `--workdir`. Ne pas intégrer cette
branche de diagnostic aux commits des ressources statiques. Les tailles et
mesures historiques ci-dessous incluent ce diagnostic.

Ne flasher qu'après confirmation explicite. Fermer les autres onglets du routeur
et éviter les autres clients HTTP. Dans le navigateur intégré, ouvrir une page du
routeur local `http://192.168.0.55` ; une page inerte comme `/ajax_diag` évite le
polling de l'accueil. Exécuter le contenu de `tools/measure-static-assets.js` dans
sa console, d'abord sur le firmware actuel, puis après OTA autorisée.

Le script découvre les six ressources statiques de l'accueil, exclut les deux
petits endpoints dynamiques, et effectue quatre passes : six essais isolés puis
un seul lot parallèle de six. Il consomme chaque réponse, lit `/ajax_diag` avant
et après, calcule `avant.minSinceRead - après.minSinceRead`, et espace les essais
d'une seconde. Une lecture de purge puis 500 ms de repos précèdent chaque essai.
Il arrête sur erreur HTTP, timeout (15 s), diagnostic invalide ou tas libre avant
essai inférieur à 8 Kio. Pas d'écriture ni de modification des réglages.
Les résultats JSON restent dans `staticAssetMeasurements` et sont affichés en console.
`staticAssetMeasurementPromise` permet d'attendre la fin depuis l'automatisation.
`decodedBytes` est la taille après décompression navigateur ; `wireBytes` est le
`Content-Length` reçu, donc la taille gzip transférée (ou `null` si absent).
Comparer médianes et étendues des quatre passes, avec le bruit attendu de ±2 Ko.

Pour isoler les effets, comparer les builds de cette branche
`--no-minify --no-gzip`, `--no-gzip`, puis le build par défaut. Une comparaison au firmware
antérieur inclut aussi l'effet de cette extraction et de deux requêtes supplémentaires
au chargement complet. Tester aussi visuellement les couleurs, bi-sonde, paramètres,
actions et OTA. Ne pas conclure sur le tas à partir du seul gain flash.

Exemple de commandes (à n'exécuter qu'après autorisation OTA) :

```bash
BUILD_TAG=assets-test tools/build-binary.sh --workdir /chemin/du/worktree
SR_FLASH_CONF=/Users/tsabran/dev/private/Solar-Router-F1ATB/flash.conf \
  tools/flash.sh --ota /chemin/release-explicite
```

Le cookie reste dans `CADDY_ACTIVE_COOKIE`, utilisé par la configuration du
reverse proxy ; ne jamais l'envoyer au routeur HTTP local ni l'afficher.
Ne pas utiliser `--push-ref` ; refuser toute proposition de push.

#### Mesures réelles du 14 octobre 2026

OTA autorisées et effectuées avec `flash.sh --ota`, version finale relue :
`17.30 (assets-gzip-20261014c 949f1493-dirty)`. Aucun push. Référence sur
`measure-opt2`, puis quatre passes navigateur sur la version finale, depuis
`/ajax_diag` sans polling de l'accueil, avec le protocole ci-dessus.

| Ressource | Creux médian avant | Après minification + gzip |
|-----------|-------------------:|--------------------------:|
| CommunCSS | 4 678 o | 2 234 o |
| ParaCommunJS | 3 890 o | 666 o |
| MainJS1 | 7 254 o | 5 602 o |
| MainJS2 | 9 732 o | 8 616 o |
| MainJS3 | 8 292 o | 1 318 o |
| CommunCouleurJS | 6 992 o | 56 o |
| Les 6 en parallèle | 8 160 o | 1 086 o |

Pour les six en parallèle : étendue avant **7 180–9 812 o**, après **32–3 156 o**,
soit une baisse médiane observée de **7 074 o**. Les essais individuels sont
beaucoup plus bruités que les ±2 Ko attendus : plusieurs différences sont
négatives, car l'activité de fond varie entre les fenêtres avant/après.
Ne pas interpréter ces nombres comme une consommation mémoire négative, ni
garantir un gain précis par fichier. L'échantillonneur lit une fois par tick
FreeRTOS et peut aussi manquer les creux très brefs des petits transferts gzip.
Le minimum depuis le boot, remis à zéro par l'OTA, n'est pas comparable.
Au dernier contrôle hors protocole : tas libre **42 388 o**, minimum depuis ce
reboot **11 908 o**. D'autres activités continuent donc à produire des creux ;
ce changement ne garantit pas à lui seul l'absence de pression mémoire.

Ces essais comparent l'effet combiné minification + gzip + extraction des
préfixes dynamiques, pas la minification isolée. Les six corps transférés
passent de **57 941 à 11 949 o (−79,4 %)**, hors les deux petits endpoints
dynamiques désormais séparés (236 o au total). La RAM statique reste identique.

Contrôles HTTP réels : HTML `no-store`, six statiques `gzip` + `Vary` + cache
immutable, dynamique non compressé et `no-store`, ancienne URL `no-store`,
refus explicite de gzip → 406 avec un seul `Cache-Control: no-store`.
Cache navigateur chaud vérifié sur MainJS1 : premier `transferSize=2310`,
second `transferSize=0`, corps gzip de 2 010 o et décompressé de 5 997 o.
Le cache peut donc éviter entièrement le transfert après le premier chargement.

Validation du firmware mesuré : **113 tests C++, 15 JavaScript et 13 tests Python**, plus
parsing de tous les JS préparés et relecture C++ des métadonnées gzip. Le seul
avertissement C++ natif est celui préexistant sur le tableau de taille variable
dans `Actions.cpp`.
Après déplacement de la branche sur `enphase-envoy-fix` (`ec8e1c6`), sans diagnostic
firmware, la suite passe à **114 tests C++, 15 JavaScript et 13 tests Python**.
Le build `assets-gzip-nodiag-20261014` est validé à **1 732 800 o**
(212 800 o libres dans la partition app), sans nouveau flash du routeur.

#### Fusion de fichiers : intérêt conditionnel

`assets.json` inclut aussi `bundle_estimates`, sans fusionner les fichiers servis.
Mesures sur les ressources minifiées (séparateur `;\n` entre scripts) :

| Regroupement | Requêtes évitées | Plus gros séparé | Fusionné | gzip séparés → fusionné |
|--------------|----------------:|-----------------:|---------:|-----------------------:|
| MainJS1 + MainJS2 + MainJS3 | 2 | 13 367 o | 30 515 o | 8 738 → 7 836 o |
| Les 5 JS de l'accueil | 4 | 13 367 o | 37 529 o | 11 341 → 9 970 o |
| ParaCommunJS + CommunCouleurJS | 1 | 3 654 o | 7 012 o | 2 603 → 2 470 o |
| ActionsJS1…4 | 3 | 13 238 o | 39 078 o | 12 347 → 10 788 o |
| ParaJS1 + ParaJS2 | 1 | 8 395 o | 14 886 o | 4 942 → 4 564 o |
| BruteJS1 + BruteJS2 | 1 | 9 667 o | 15 276 o | 4 549 → 4 347 o |

Sans gzip, fusionner ne réduit presque pas la flash ni les octets des corps :
seuls les en-têtes HTTP et quelques métadonnées sont économisés. En cache chaud,
les ressources versionnées ne génèrent déjà aucune requête réseau pendant leur
fraîcheur. À froid, moins de requêtes peut réduire les connexions simultanées et
leurs buffers ; une réponse plus longue peut au contraire remplir davantage les
buffers TCP. Le core ESP32 3.3.7 transmet `write_P` à `write`, puis à `send` :
il n'alloue pas un tampon de la taille totale du fichier à ce niveau.
On ne peut donc pas déduire le creux de tas de la seule taille fusionnée.

**Recommandation pour ce test : pas de fusion par défaut.** Comparer d'abord
minification/cache seuls. Un bundle commun de 7 Ko est le candidat prudent à
tester ensuite ; un bundle accueil unique de 37,5 Ko est à éviter sans mesure,
sur un tas au repos de l'ordre de 35 Ko. Avec gzip, ce dernier tomberait à
environ 10 Ko et deviendrait un candidat plus intéressant, toujours à mesurer.

Les concaténations candidates passent le parseur JS, mais ceci ne prouve pas
l'équivalence : fusionner peut changer le hoisting et la portée de déclarations.
L'ordre actuel diffère selon les pages (`CommunCouleurJS` dans le `<head>` sur
certaines, en fin de page sur l'accueil). Garder cet ordre, tester les effets au
chargement et séparer les valeurs dynamiques serait indispensable avant activation.
Ne jamais concaténer JS et CSS ni remettre les valeurs dynamiques dans un bundle
immutable.

FQBN : `esp32:esp32:esp32:PartitionScheme=custom`. Le script vérifie que le binaire tient
dans la partition app de `partitions.csv` (1900K) et avertit au-delà de 95 %.

La version du firmware devient `<version> (<label> <sha>)`, par exemple
`17.26 (main+3-20261013T1030 1aeb5a50)`. Le SHA est suffixé par `-dirty` si des
modifications locales ont été incluses. Elle apparaît au boot sur le port série, en Telnet,
dans l'interface web et dans le message MQTT `sw`.

## Labels de build

Le label (`BUILD_TAG`) nomme la release et apparaît dans la version du firmware. Il décrit
le contenu du build :

```
<base>[+N][-wip]-<AAAAMMJJ>T<HHMM>      ex. main+3-20261013T1030, enphase-envoy-fix+2-wip-20261013T1102
<RELEASE_SUFFIX>.<N>                    release taguée (--release), ex. custom.4
```

- `base` : branche du répertoire de travail (localdev, ou SHA court si HEAD détaché), `main`
  (localrelease) ou la ref passée à `--base` ; `/` devient `-`, 24 caractères au plus.
- `+N` : nombre de branches effectivement mergées (absent si aucune).
- `-wip` : des modifications non commitées sont incluses (`worktree.patch`). Un build `-wip`
  n'est reconstructible que grâce à sa ref `refs/builds/<label>` ; `flash.sh` demande une
  confirmation explicite avant de le déployer, même avec `--yes`.
- `T` sépare la date et l'heure (ISO 8601, heure locale).

`build-binary.sh` seul utilise `<branche>[-wip]-<date>T<heure>` ; un `BUILD_TAG` fourni est
pris tel quel.

## Assembler des branches : merge-and-build.sh

Le script travaille dans un clone local temporaire (`.tmp-build/merge-<mode>-<horodatage>/clone`) :
il ne modifie ni les branches ni le working tree.

| Base | Option | Contenu |
|------|--------|---------|
| localdev *(défaut)* | `--localdev` | HEAD du répertoire de travail + modifications non commitées (indexées, non indexées, fichiers non suivis non ignorés) |
| localrelease | `--localrelease` | `origin/main` après `git fetch origin main` |
| quelconque | `--base REF` | branche, tag ou commit, sans modifications locales |

Les branches de `MERGE_BRANCHES` dans `build.conf`, ou passées en arguments (qui remplacent
cette liste), sont ensuite mergées dans l'ordre, en `--no-ff`, par SHA résolu au démarrage.

### Configuration : build.conf

`build.conf` à la racine du worktree principal (ignoré par git, modèle
`tools/build.conf.example`, autre chemin via `--config FILE` ou `SR_BUILD_CONF`) :

```bash
MERGE_BRANCHES=(          # mergées dans cet ordre ; commenter une ligne pour l'exclure
  build-automation
  feature/mode-sequenceur
  ota-improve
)
RELEASE_SUFFIX=custom     # tags v<version>-custom.N (--release)
```

`build.conf` et `flash.conf` sont en syntaxe bash et sourcés par les scripts : n'y mettre
que ces affectations.

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

### Releases taguées : --release

```bash
tools/merge-and-build.sh --release --base main          # -> tag v17.26-custom.1
git push origin v17.26-custom.1                         # publication, à faire soi-même
```

Une release n'est construite que si elle est reproductible par n'importe qui depuis
origin : une seule base, aucune modification locale, base et branches mergées déjà présentes
sur origin, outillage sans modification non commitée, tests natifs exécutés (`--no-test` et
`--no-ref` refusés). Après un build réussi, le commit compilé reçoit un tag annoté local
`v<version>-<suffix>.<N>` (N = plus grand numéro existant + 1 pour cette version) dont le
message contient la composition et le SHA-256 du firmware. Le tag remplace
`refs/builds/<label>` comme `build_ref` du manifeste. Rien n'est poussé automatiquement.

`<suffix>` vient de `RELEASE_SUFFIX` dans `build.conf`, `custom` par défaut. Il
distingue ces releases de celles du mainteneur (`v17.26`).

### Depuis une branche qui n'hérite pas de build-automation

L'outillage est pris dans le répertoire du script, les sources dans le répertoire courant
(ou `--workdir`). Garder un worktree `build-automation` et l'appeler depuis n'importe quel
worktree :

```bash
alias srbuild="$HOME/dev/private/Solar-Router-F1ATB.worktrees/esp32-solar-router-build-automation/tools/merge-and-build.sh"
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
releases/Solar_Router_V17_26_main+3-20261013T1030/
  Solar_Router_V17_26_main+3-20261013T1030.ino.bin      # firmware (OTA / flash)
  Solar_Router_V17_26_main+3-20261013T1030.ino.elf.gz   # symboles, pour décoder un crash
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

L'arbre exact compilé est conservé dans le dépôt sous `refs/builds/<label>` (ou le tag pour
`--release`), créée avant la compilation ; `--no-ref` la désactive (déconseillé, surtout avec
`-wip`) :

```bash
git for-each-ref refs/builds
git log --oneline --graph refs/builds/main+3-20261013T1030
git switch --detach refs/builds/main+3-20261013T1030
git update-ref -d refs/builds/<label>                   # supprimer une ref de build
```

Ces refs sont locales. Pour qu'un firmware déployé reste reconstructible ailleurs,
`flash.sh` propose, après un déploiement réussi, de pousser sa ref vers origin si elle n'y
est pas (question séparée de `--yes` ; `--push-ref` pour pousser sans demander ; en non
interactif, la commande `git push origin <ref>` est affichée).

### Date « Software Info »

`Software Info: <date>` vient de `__DATE__`/`__TIME__` dans le core ESP32
(`chip-debug-report.cpp`). Sans `--build-path`, arduino-cli réutilise le `core.a` en cache
(`~/Library/Caches/arduino/cores/…`) : la date affichée est celle de la première compilation
du core, pas celle du firmware (d'où « Mar 18 2026 » sur un build du 29/06). `build-binary.sh`
utilise un `--build-path` neuf à chaque build, la date est donc juste, mais c'est
`BUILD_TAG`/SHA dans la version qui fait foi.

## Déployer : flash.sh

Sans argument de release, la plus récente de `releases/` est utilisée. Le SHA-256 est vérifié
contre `SHA256SUMS` et une confirmation est demandée (`--yes` pour l'omettre ; un build
`-wip` demande une confirmation de plus, que seul `SR_ALLOW_WIP=1` permet d'omettre). Après
un déploiement réussi, la ref de build est proposée au push (voir Traçabilité).

```bash
tools/flash.sh --serial --dry-run                    # affiche la commande esptool
tools/flash.sh --serial /dev/cu.usbserial-0001       # flash USB
tools/flash.sh --ota 192.168.1.50 Solar_Router_V17_26_main+3-20261013T1030
tools/flash.sh --ota                                 # cible = OTA_HOST de flash.conf
tools/flash.sh --ota https://router.example.org --cookie auth_token=PROXY_COOKIE \
               --cookie-domain example.org           # à distance, derrière un reverse proxy
```

**OTA à distance** : `--ota` accepte une URL `https://…` (préfixe de chemin autorisé). Avec
`--cookie NAME=ENVVAR` (ou `OTA_COOKIE` dans `flash.conf`), le cookie `NAME` est envoyé avec la
valeur de la variable d'environnement `ENVVAR` : la valeur n'est jamais écrite dans un log,
ni passée sur la ligne de commande (curl la lit via un descripteur de fichier). Garde-fous :
un cookie exige `--cookie-domain`/`OTA_COOKIE_DOMAIN`, la cible doit être en `https://` sur ce
domaine ou un sous-domaine (sinon refus avant toute requête), les redirections ne sont pas
suivies. Une réponse 3xx/401/403 sur `/ParaFixe` (cookie absent ou expiré) arrête le flash
avant l'envoi.

**Configuration** : `flash.conf` à la racine du worktree principal (ignoré par git, modèle
`tools/flash.conf.example`, autre chemin via `SR_FLASH_CONF`) fournit les cibles par défaut :
`OTA_HOST=192.168.1.50` (ou une URL), optionnels `OTA_COOKIE`, `OTA_COOKIE_DOMAIN` et
`SERIAL_PORT=/dev/cu.usbserial-0001`. Une cible passée en argument est prioritaire.

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
- **OTA** : envoie l'app sur `<cible>/update` (page « Mise à jour » du routeur), attend
  le redémarrage puis vérifie que `/ParaFixe` renvoie une version contenant `(<BUILD_TAG> `.
