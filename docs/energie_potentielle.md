# Bornes d'energie pendant la regulation autonome

## Base et portee

Implementation rebasee sur `feature/mode-sequenceur` (base `c841cd1`, version 17.26),
incluant le socle de tests de `build-automation`.
Ce n'est pas une reproduction du firmware personnalise 17.25
`localdev-20260629-170300`. Un portage et une validation sur ce firmware restent
necessaires avant toute installation. Aucun reglage du RMS en service n'est
modifie par ce travail, aucune chauffe de test ni installation n'est effectuee.

Ces deux indicateurs sont des **bornes conventionnelles de commande**,
pas une mesure du gaspillage, du courant des resistances ou de l'energie recue
par le ballon. La classification quotidienne, le cout de l'appoint et la
chronologie appoint/injection restent dans Home Assistant.

## Option dans l'interface

Dans le panneau sequenceur, cocher **Estimer les pertes d'energie
potentiellement evitables**. L'infobulle precise les deux compteurs, leur
caractere borne, les exclusions et les prerequis. Le thermostat n'est pas
mesure ; ces estimations ne sont pas des pertes reelles prouvees.

Les formules ne dependent pas intrinsequement d'Envoy ni du demi-sinus.
En revanche, cette implementation est volontairement limitee : seul Envoy
alimente l'observateur avec puissance signee et controles de fraicheur, et
seules les sorties resistives locales en demi-sinus sont admises pour
convertir l'ouverture en fraction de puissance nominale. Un seul groupe
sequenceur est selectionnable, avec puissances nominales explicites.
L'extension a d'autres sources ou modes necessiterait de valider ces
contrats ; changer le libelle ne leve aucune restriction.

## Inventaire du code existant

- [Source_EnphaseEnvoy.ino](../Source_EnphaseEnvoy.ino) lit
  `/ivp/meters/readings`, en HTTPS (V7). Le lecteur amont prend le deuxieme
  compteur de la reponse pour le reseau : `activePower` global en triphase,
  ou du premier canal en monophase. Le suivi utilise la meme selection :
  global en triphase, premier canal en monophase, avec le `timestamp` de
  l'entete de ce compteur.
  La puissance est positive en soutirage. Le chemin historique la borne avec `PfloatMax`,
  la tronque en W et la separe en `PuissanceS_M_inst`/`PuissanceI_M_inst`.
- [Stockage.ino](../Stockage.ino), `filtre_puissance`, applique sur la difference
  signee un filtre RC `0.3 * nouveau + 0.7 * ancien` lorsque `LissageLong` est
  actif (dont demi-sinus). Sinon, il prend directement la valeur. Le coefficient
  est par appel, pas par temps ecoule.
- [Solar_Router_V17_26.ino](../Solar_Router_V17_26.ino) regule avec
  `PuissanceS_M - PuissanceI_M` (entiers issus du RC), toutes les ~200 ms.
  Les historiques a 2 s sont des instantanes, pas des integrales de puissance.
  La boucle Envoy attend **2000 ms** apres la fin de chaque lecture,
  en plus du temps HTTP. Les ~440 ms observes sur le firmware personnalise ne
  decrivent donc pas cette branche.
  Sur l'installation utilisateur avec lecteur optimise, la cadence observee
  est d'environ 500 ms. `enphase-envoy-fix` attend 300 ms + soutirage/10 apres
  lecture ; sa connexion TLS est reutilisee. Cette branche de monitoring
  reste basee sur le sequenceur, sans rejouer les commits Envoy. La fusion
  du lecteur optimise conserve le point d'observation commun
  [EnvoyReadObserver.h](../EnvoyReadObserver.h), present a l'identique dans
  les deux branches. Il accepte `String` et `char[]` et conserve la selection
  global/canal.
- [EnvoiMQTT.ino](../EnvoiMQTT.ino) publie ces puissances filtrees a `MQTTRepet`
  et des energies maison existantes. L'energie Envoy historique utilise les
  differences de `actEnergyDlvd` et `actEnergyRcvd`, pas ces nouveaux calculs.
- [Actions.cpp](../Actions.cpp), `ParaEnCours`, decide le type effectif en
  fonction des horaires, temperatures, tarif, dependances et `tOnOff`.
  Types 3/4 : autonome ; type 2 : ON programme/force ; type 1 : OFF.
  La fonction modifie l'hysteresis thermique : le monitoring **ne la rappelle
  pas**, il observe le resultat deja utilise par la regulation.
- Le sequenceur distribue les commandes aux relais, puis recalcule une
  ouverture de groupe ponderee. Le monitoring utilise les `Retard[]` entiers
  **des relais apres distribution et forcages**, jamais l'ouverture virtuelle
  du sequenceur. En demi-sinus, `100 - Retard` est le rapport de commande de
  l'accumulateur de phase, donc une fraction conventionnelle de puissance
  pour une resistance. Cela n'est pas un courant mesure.
- Les parametres et energies existants sont sauvegardes dans LittleFS.
  Le nouveau suivi ne cree aucun fichier, aucune sauvegarde periodique, et
  n'est pas ajoute aux sauvegardes d'energie/minuit/redemarrage.

## Activation explicite

Dans Actions, cocher **Suivi des bornes d'energie autonomes** sur le sequenceur
du chauffe-eau, **un seul groupe**. Par defaut le suivi est desactive.
La case affirme que les charges de ce groupe sont resistives. Le firmware
ne peut pas verifier la nature physique d'une charge.

Tous ses membres doivent etre des relais locaux en **demi-sinus**, avec GPIO
configure et `PuissanceCharge > 0` explicite. Pour trois resistances de 1000 W,
ces valeurs doivent etre configurees par l'utilisateur apres verification ;
elles ne sont pas supposees par le monitoring. Le fallback 1000 W de la
regulation existante est conserve mais **refuse par le monitoring**.

Decoupe de phase, PWM, trains/multi-sinus, relais distants, membres inactifs,
groupe vide, plusieurs groupes selectionnes ou puissance manquante rendent
les compteurs indisponibles. L'extension a d'autres modes necessiterait une
validation propre de leur rapport ouverture/puissance et de leur fenetre.
Changer le groupe conserve les cumuls en RAM : ne pas melanger plusieurs
installations dans la meme serie HA sans le signaler.

## Mesure et integration sans moyennage supplementaire

Le suivi a une voie d'observation distincte, sans modifier les variables,
coefficients, seuils ou sorties de la regulation. Un parseur ArduinoJson
**filtre** ne conserve que `timestamp` et `activePower` dans l'entete du
compteur reseau ; il valide leur presence/type et les valeurs finies.
L'entete est copiee avant que le lecteur amont ne la remplace par les canaux,
et la mesure n'est transmise qu'apres lecture complete reussie.
Les callbacks sont enregistres au debut du setup, avant le lancement de la
tache de mesure. Sans monitoring, l'observateur n'a aucun callback enregistre :
aucun parsing additionnel ni publication ne sont effectues par le lecteur.
Avec monitoring, son destructeur transmet le resultat a la sortie de la
fonction. Toute sortie avant `complete()` invalide la mesure, y compris les
nouveaux chemins d'erreur/retry du lecteur optimise. Les fonctions reseau,
delais, buffers et calculs preexistants ne sont pas modifies.
Le parseur de l'ancien format reports/consumption reste teste mais n'est
pas utilise par cette base 17.26.
Une puissance nulle est valide, une cle absente n'est pas un zero.
Le parseur additionnel est inactif lorsque le monitoring n'est pas configure.

`timestamp` a une resolution d'une seconde : son egalite **n'est pas** un
critere de rejet. Chaque lecture complete valide remplace la puissance,
y compris lorsque le timestamp est identique et la puissance change.
Une valeur identique est aussi acceptee : une puissance constante ne prouve
pas que la source est bloquee. Cela ne signifie pas que chaque lecture est
une mesure physique independante.
La derniere valeur est maintenue jusqu'au prochain rapport, au maximum
**3 s** apres reception d'une lecture valide.
Il faut une horloge RMS a l'heure : les rapports deja vieux de plus de 3 s,
ou dates de plus de 2 s dans le futur, sont refuses. Un retour en arriere de
`timestamp`, un echec HTTP/parsing, une puissance au-dela de `PmaxReseau`
ou une pause de boucle >1 s interrompent l'integration.
L'age du timestamp est aussi controle dans la boucle de suivi, meme sans
nouvelle reponse : des reponses identiques ne prolongent pas indefiniment
une mesure dont la date est devenue perimee.
La reprise commence sans rattrapage de l'interruption.

`SourcePeriodMs` donne le dernier ecart strictement positif entre horodatages
source acceptes, **pas** la cadence des lectures ni la duree HTTP.
Les lectures dans la meme seconde ne modifient pas ce diagnostic.
Il faut verifier sur l'Envoy reel que
`timestamp` correspond bien au renouvellement de la mesure, et non seulement
a la generation du rapport. Sans cette validation, on ne pretend pas
connaitre la frequence physique de conversion ni avoir des echantillons
independants. En l'absence d'horodatage exploitable, le suivi est indisponible.

Le RC existant est conserve pour la regulation. Les compteurs utilisent la
**puissance active signee valide de la source, avant le RC RMS**, sans ajouter
de moyenne sur 2 s : la cadence de lecture n'etablit pas la duree de moyennage
interne Envoy et ne justifie pas un tel lissage. L'integration conserve la
derniere valeur valide entre receptions, avec les temps `millis()` reels.
Le maintien de valeur source est borne par la fraicheur ci-dessus ; ce n'est
pas un maintien pendant 30 s. La boite aux lettres coeur 0/coeur 1 est protegee
par section critique ; les cumuls et commandes appartiennent au coeur 1.
Les instants de reception sont utilises pour couper les intervalles ; si la
notification arrive apres un tick de boucle, on ne remonte pas le temps.

La source ne restitue pas chaque demi-sinus de 10 ms. Elle fournit une
puissance active, pas une serie brute de produits tension/courant.
On ne pretend toutefois pas connaitre sa fenetre interne de moyennage.
Si cette source presente des alternances de signe dues a un artefact de
modulation plutot qu'a des variations reelles, leur integration separee
peut surestimer les echanges ; un filtre ne devrait etre ajoute qu'apres
caracterisation de ce phenomene. Retard, aliasing et moyennage source restent
des limites :
**aucune equivalence a la facturation Linky n'est revendiquee**.
Voir [les modes de regulation F1ATB](https://f1atb.fr/fr/documentation-routeur-photovoltaique-f1atb/pages-web-ihm/action/routeur-photovoltaique-modes-de-regulation/).

Sur chaque intervalle eligible, on utilise la puissance source et les
commandes en vigueur sur **le meme intervalle** :

```text
P = derniere puissance reseau signee valide
C = somme(P_nominale * ouverture / 100)
M = somme(P_nominale * (1 - ouverture / 100))

soutirage_potentiel_W = min(max(P, 0), C)
injection_potentielle_W = min(max(-P, 0), M)
energie_Wh += puissance_potentielle_W * duree_ms / 3600000
```

Les changements de commande et d'eligibilite sont decoupes avant d'enregistrer
le nouvel etat : le temps precedent utilise la commande precedente, pas
la nouvelle commande retroactivement. Il n'y a plus de bloc de 2 s a attendre
ou a abandonner. Une interruption ne modifie pas les cumuls acquis.
La borne reste conventionnelle, pas une attribution certaine des appels
de puissance maison au ballon.

### Triphase et chauffe-eau monophase

Le suivi observe le bilan **global signe des trois phases**, comme la
regulation triphasee actuelle : choix confirme par l'utilisateur pour suivre
la somme arithmetique reseau, pas la phase du ballon seule.
Les puissances nominales et commandes portent
sur le groupe chauffe-eau (ici trois resistances sur une meme phase).
Les echanges sur les autres phases peuvent compenser ceux de la phase du
ballon dans ce bilan. Ces compteurs ne sont donc pas des mesures d'import
ou d'export de la phase du chauffe-eau seule. L'equivalence avec les index
factures n'est pas etablie. La detection mono/triphase et le choix du deuxieme
compteur de la reponse restent ceux du lecteur amont, sans changement.

## Eligibilite et thermostat

- Seulement types effectifs autonomes 3/4 du sequenceur.
- Tout `tOnOff != 0` dans ce RMS (ON **ou** OFF, individuel ou global par
  commandes multiples), ON programme du groupe et modulation de test excluent
  le comptage. Pas de nouvelle logique de boost : le gain PID CACSI n'est pas
  un appoint, il reste un gain de regulation.
- Soutirage : borne nulle si toutes les commandes sont nulles.
- Injection : ouverture zero **incluse** si le groupe est eligible ; marge
  nulle a 100 %. Hors planning, condition thermique/tarif/dependance non
  satisfaite ou sonde requise invalide : pas de capacite fictive comptee.
- Aucun thermostat n'est mesure. Une coupure physique non connue du RMS peut
  donc surestimer la capacite commandable et la puissance commandee. La
  temperature de 66 C et le rearmement 59-60 C ne sont pas des certitudes.
  **Aucun seuil 66/60 automatique n'est ajoute.** Une exclusion simple possible
  est l'hysteresis thermique deja disponible sur le planning du sequenceur,
  avec une limite haute choisie prudemment sous la coupure observee et un
  seuil bas valide sur site. Cela modifierait le planning de regulation :
  proposition uniquement, a faire avec accord explicite, pas par ce patch.
- La regulation autonome n'implique pas a elle seule la presence de soleil :
  les horaires/conditions existantes font foi, aucun detecteur jour/nuit ajoute.

## MQTT et Home Assistant

Publication au meme rythme que le service MQTT existant (par exemple 30 s),
sur `<StateTopic>/regulation`, separee du gros payload existant pour ne pas
en depasser le buffer. Les cumuls sont `PotentialImportWh` et
`PotentialExportWh`. Discovery reutilise prefixe, identifiant et device RMS :
`device_class: energy`, unite Wh, `state_class: total_increasing`, precision
d'affichage 0.001 Wh. Calcul en `double`, publication a six decimales de Wh.

Disponibilite : combinaison du LWT du RMS, de `Available` dans le payload
(configuration et source valides, independamment de la fraicheur de la mesure :
un cumul reste affiche et se fige simplement pendant une perte de mesure) et
expiration apres `2 * MQTTRepet + 10` secondes. `Status` explique notamment
`missing_nominal_or_unsupported_output`, `multiple_groups`,
`invalid_or_stale_measurement`, `forced` ou `not_available_or_scheduled_on`.
Les cumuls restent disponibles lors d'une exclusion de comptage si
configuration/source sont valides : `Eligible` indique si le suivi compte.
`GridW` est la derniere puissance source valide ; `PotentialImportW` et
`PotentialExportW` sont ses bornes avec la commande courante, nulles hors
eligibilite. Ces diagnostics sont remis a zero lorsque la mesure est invalide.
`Integration: source_power_sample_hold` indique l'absence de moyenne RMS
supplementaire. `CommandedW`/`HeadroomW` sont la commande courante.
Ce ne sont pas de nouvelles puissances mesurees au chauffe-eau.

**Choix confirme : aucune persistance supplementaire.**
Les cumuls sont monotones depuis le demarrage, pas entre redemarrages.
Ils repartent a zero au boot ; aucun reset quotidien. HA traite une baisse
comme un reset de `total_increasing` et conserve le quotidien via un
`utility_meter` de cycle `daily`. Les Wh non publies avant une coupure sont
perdus. HA ne peut pas reconstruire un reset
invisible entre deux publications : le suivi n'est pas un compteur certifie.
Les sauvegardes ordinaires des parametres incluent seulement la case
`SuiviEnergie` ; cocher la case n'ajoute aucune ecriture periodique.

## Validation locale et comparaison reelle

Les tests natifs couvrent puissance constante/pas variables, separation des
signes des valeurs source sans compensation supplementaire sur 2 s,
changements intra-seconde, lectures identiques, bilan global triphase avec
phases de signes opposes, plafonds, 0/40/100 %, aggregation
des trois resistances, puissance manquante/mode non supporte, transitions
autonome/exclusion, donnees repetees/perimees/invalides, interruption, wrap
de `millis()`, notification retardee, reset au boot et parseur Envoy.
La suite `MetricsFirmware` inclut directement l'observateur firmware avec des
entrees simulees : groupe et puissances explicites, tous les forcages ON/OFF,
ON programme/hors autonomie, sonde invalide, modes/GPIO non supportes,
selection multiple, source non supportee, et non-ecriture des commandes.
Les groupes retenus doivent suivre les memes restrictions que le regulateur :
parent d'index strictement positif et autonome (`IdxSequenceur == -1`).
Un groupe sur le Triac ou avec une reference negative invalide est rejete.
Une horloge simulee verifie aussi l'expiration de l'horodatage source
sans nouvelle reponse HTTP, independamment du delai monotone de reception.
Le point d'observation commun est teste avec des chaines et buffers `char[]`,
la selection globale/canal, les sorties anticipees, le canal invalide et
l'absence de callbacks quand la feature n'est pas presente.

Les anciens executables sont maintenant decouverts par le runner commun dans
`tests/metrics/`, avec assertions toujours actives. La suite complete contient
74 cas : 34 cas C++ et 4 cas JavaScript propres aux metriques, plus les 36 cas
Enphase/sequenceur herites. Les suites additionnelles verifient les fonctions
reelles de publication MQTT, l'opt-in web, une integration de reference sur
une heure et le couplage au regulateur. Une simulation de 60 s compare les
commandes, integrateurs et sorties avec/sans suivi, avec mesures toutes les
2 s ; aucune modification de ces commandes n'est attendue.

```sh
./tests/run-tests.sh
# Si ArduinoJson n'est pas dans le dossier Arduino habituel :
ARDUINOJSON_INCLUDE=/chemin/ArduinoJson/src ./tests/run-tests.sh Metrics
```

Le broker MQTT, Home Assistant, les sections critiques entre coeurs et
LittleFS ne sont pas simules integralement. Les schemas et publications
MQTT sont interceptes, sans connexion a l'installation en service.

Compiler aussi le sketch avec le coeur ESP32 et la table de partitions
du depot (app0 : 1900 KiB) ; aucune commande d'upload dans cette validation.
Le dossier de staging Arduino doit s'appeler `Solar_Router_V17_26` et
contenir les sources a la racine du depot (pas les tests).
Commande utilisee, avec les chemins de staging/build adaptes :

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property upload.maximum_size=1945600 \
  --build-path /chemin/build /chemin/Solar_Router_V17_26
```

Validation historique avant ce rebase : coeur ESP32 3.3.7, ArduinoJson 7.4.3, tests avec ASan/UBSan,
syntaxe des quatre scripts Actions embarques et `git diff --check` passes.

Validation apres rebase sur le sequenceur teste : **74/74 cas** passent.
Les **62 cas C++** passent aussi avec ASan/UBSan et `NDEBUG`, sans desactiver
les assertions du harness. La compilation locale avec le coeur ESP32 3.3.7
reussit : programme de 1 909 919 octets, RAM statique de 96 840 octets.
Le fichier applicatif fait **1 910 064 / 1 945 600 octets** (98 %), soit
**35 536 octets libres**. Aucun upload n'a ete effectue.

Les chiffres de comparaison ci-dessous proviennent des validations anterieures :
Apres extraction du point d'observation commun, les deux branches et leur
fusion simulee compilent :

| Variante | Programme (maximum 1 945 600 octets) | RAM statique |
| --- | --- | --- |
| Monitoring et sequenceur | 1 906 535 octets (97 %) | 96 832 octets (29 %) |
| Envoy optimise seul | 1 886 835 octets (96 %) | 99 392 octets (30 %) |
| Fusion monitoring + Envoy optimise | 1 912 915 octets (98 %) | 99 736 octets (30 %) |

La fusion des branches adaptees est automatique, sans conflit. Cette
validation concerne leurs revisions testees, pas d'eventuelles modifications
ulterieures. Aucun merge reel, upload ou changement de configuration du RMS
n'a ete effectue. La marge programme de la fusion est de 32 685 octets.
Les lignes preexistantes du sketch principal et de la source Envoy sont
identiques a la base sequenceur apres retrait des seuls hooks d'observation
et du point d'observation commun.
Cela verifie l'absence de modification des calculs/sorties ; le cout temporel
du suivi optionnel doit encore etre observe sur le materiel reel.

La comparaison reelle **reste a faire apres autorisation de deploiement** :

1. Verifier les timestamps Envoy, cadence de renouvellement et age ; conserver
   la cadence HTTP distincte. Valider le rapport ouverture/puissance et les
   exclusions sur les donnees de fonctionnement normal, sans chauffe forcee.
2. Sur une periode continue, sans reboot, conserver debut/fin des cumuls,
   temperatures, etats autonomes/forcages et diagnostics MQTT.
3. Comparer les differences des cumuls avec l'integration HA des puissances
   echantillonnees. Comparer des quantites identiques : les deux nouveaux
   compteurs sont **bornes**, contrairement a l'import total maison.
4. Signaler trous de donnees et intervalles exclus. Ne pas comparer directement
   2.41 Wh d'import maison a un compteur d'import potentiel borne.
5. Ne pas extrapoler dix minutes a une annee. Les tests de puissance signee
   ne prouvent pas la compensation temporelle sur l'Envoy ou le Linky reel.
