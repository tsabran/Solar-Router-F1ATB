# ⚡ Routeur Photovoltaïque F1ATB

Routeur photovoltaïque basé sur **ESP32**, permettant d’optimiser l’autoconsommation d’une installation solaire en redirigeant les surplus de production vers des charges (chauffe-eau, chauffage, pompe, etc.) au lieu de les injecter sur le réseau.

[**Détails Routeur F1ATB**](https://f1atb.fr/fr)

---

## Nouveautés depuis l'import de la version 17.30

Ce dépôt enrichit la version **17.30 de F1ATB** ; les ajouts ci-dessous sont
propres à cette version personnalisée, pas à une nouvelle release officielle.
Cette synthèse couvre les **65 commits ajoutés après l'import**
[`67b6d06`](https://github.com/tsabran/Solar-Router-F1ATB/commit/67b6d06)
jusqu'à [`4b204b4`](https://github.com/tsabran/Solar-Router-F1ATB/commit/4b204b4)
inclus, au 14 octobre 2026.

### Pilotage séquentiel de plusieurs charges

- Nouveau mode **Séquenceur de relais** dans la page **Actions** : une action
  virtuelle, sans GPIO, porte la régulation et les conditions du groupe
  (horaires, température, tarifs, dépendances).
- La puissance est répartie dans l'ordre des actions, en tenant compte de la
  puissance nominale de chaque charge, même si les charges sont différentes.
  Exemple : pour 3 résistances de 1 000 W et une demande de 1 500 W, les
  ouvertures sont de **100 %, 50 %, 0 %**, plutôt que 50 % sur chaque sortie.
  Hors forçages, une seule charge est donc modulée à la fois.
- Plusieurs groupes indépendants sont possibles. Les forçages individuels
  restent pris en compte et l'ouverture affichée du groupe reflète les
  commandes effectivement appliquées. Une référence de séquenceur invalide
  entraîne l'arrêt de l'action concernée, avec journalisation.
- Les actions existantes restent autonomes par défaut. L'action 0 (Triac)
  ne peut être ni séquenceur ni relais séquencé.

Configuration et exemples : [guide utilisateur du séquenceur](docs/mode_sequenceur_guide_utilisateur.md).
La réduction des commutations simultanées ne constitue pas une mesure du gain
sur les harmoniques ni une certification de conformité électrique.

### Estimations d'énergie et intégration Home Assistant

- Suivi **optionnel, désactivé par défaut**, du **soutirage potentiellement
  évitable** et de l'**injection potentiellement captable**, en Wh, sans
  modifier la régulation.
- Activation dans le panneau du séquenceur : un seul groupe, des charges
  résistives locales en **demi-sinus**, des puissances nominales explicitement
  renseignées et une mesure réseau **Enphase Envoy** sont nécessaires.
- Les cumuls et l'état du suivi sont visibles dans **Données brutes**.
  Publication MQTT sur `<StateTopic>/regulation` avec les champs
  `PotentialImportWh` et `PotentialExportWh`, et découverte automatique de
  deux capteurs d'énergie Home Assistant.
- Une mesure périmée ou invalide suspend l'intégration sans effacer les cumuls.
  Avec une configuration et une source compatibles, ceux-ci restent disponibles,
  mais figés. Les forçages ON/OFF, le ON programmé et les périodes hors
  régulation autonome sont exclus du comptage.

**Limites importantes :** ce sont des bornes estimées à partir de la puissance
réseau et des commandes, pas une mesure des pertes réelles ni de l'énergie
reçue par le chauffe-eau. Le thermostat physique n'est pas mesuré. Les cumuls
restent en RAM et **repartent à zéro au redémarrage**, sans écriture flash
périodique supplémentaire. En triphasé, le bilan réseau utilisé est global,
pas celui de la seule phase du chauffe-eau ; aucune équivalence à la
facturation Linky n'est revendiquée.

Détails, prérequis et exploitation HA : [suivi de l'énergie potentielle](docs/energie_potentielle.md).

### Lecture Enphase Envoy plus réactive et plus robuste

- Réutilisation de la connexion TLS et du cookie de session local Envoy pour
  éviter une reconnexion et une authentification complètes à chaque lecture.
  Le délai entre la fin d'une lecture et la suivante est fixé à **300 ms**,
  sans ralentissement supplémentaire en cas de soutirage ; la cadence réelle
  dépend toujours du temps de réponse de la passerelle.
- Jeton conservé dans LittleFS et réutilisé au démarrage : pas de nouvel appel
  cloud si le jeton chargé est encore accepté. Renouvellement en cas de refus
  **401/403**, ou nouvelle tentative si le jeton manque, avec un délai minimal
  de **5 minutes** entre demandes. Le renouvellement systématique tous les
  30 jours est supprimé.
- Prise en charge des réponses réelles contenant jusqu'à **8 compteurs**,
  des en-têtes HTTP longs et du JSON mis en forme. Contrôles renforcés des
  données malformées ou tronquées et des erreurs réseau ; après plus de
  **10 secondes sans lecture valide**, les puissances sont remises à zéro
  et signalées invalides.
- La page **Données brutes** expose les temps de connexion, d'authentification
  et de lecture, ainsi que les compteurs de succès, d'erreurs et de reconnexions.

### Interface web allégée et mémoire mieux utilisée

- Le build en ligne de commande **minifie et compresse en gzip** les ressources
  web embarquées. Les ressources statiques sont versionnées par leur contenu :
  le navigateur peut les conserver en cache et les recharge lorsqu'elles changent.
  Les pages HTML et les valeurs dynamiques restent non cachées.
- À titre de comparaison documentée sur le profil ESP32 3.3.7, le binaire est
  passé de **1 921 472 à 1 733 824 octets**, soit environ **183 Kio économisés**.
  Ce résultat concerne les builds comparatifs du 14 octobre, pas une taille
  garantie pour tous les builds ni une mesure du tas libre en fonctionnement.
- Envoi progressif des historiques et réduction des copies de chaînes côté
  serveur et lecteur Envoy. Le tampon Linky de **4 000 octets** n'est désormais
  alloué que lorsque cette source est initialisée.
- La compilation directe dans l'IDE Arduino reste possible avec les sources
  lisibles, sans dépendance Python et sans la préparation minification/gzip.

### Mises à jour OTA plus lisibles

- Distinction entre **envoi du fichier**, **validation par le routeur** et
  **attente du redémarrage** ; protection contre les doubles envois et
  affichage explicite des erreurs.
- Vérification du retour du routeur, bornée à **80 secondes**, puis nettoyage
  des caches accessibles au navigateur et rechargement de la page OTA.
  Si le routeur ne répond pas, un lien de rechargement manuel est proposé.

### Protection des mots de passe dans la page Paramètres

La réponse `/ParaFixe` masque désormais les mots de passe **WiFi, Enphase et
MQTT**, au lieu de les renvoyer en clair au navigateur. Une sauvegarde avec le
masque inchangé conserve le secret ; saisir une nouvelle valeur le remplace
et vider le champ l'efface. Ce changement ne constitue pas une authentification
générale de l'interface : son accès réseau doit toujours être maîtrisé.

### Builds traçables, déploiement et tests automatisés

- Profil de compilation avec versions de plateforme et bibliothèques figées,
  archivage du firmware avec manifeste, checksums et provenance Git.
  Le **label du build et son SHA Git** sont affichés dans l'interface, au boot,
  en Telnet et dans les informations MQTT du routeur.
- Assemblage de branches dans un clone temporaire sans modifier le répertoire
  de travail ; scripts de flash **USB ou OTA**, y compris via un reverse proxy
  HTTPS, journal des déploiements et confirmation spécifique pour les builds
  contenant des changements non commités.
- Tests natifs C++, simulations et tests JavaScript/Python couvrant le lecteur
  Envoy, le séquenceur, les métriques/MQTT, l'OTA et les ressources web.
  Ils ne remplacent pas la validation réseau et électrique sur matériel.
- Diagnostics mémoire réservés aux builds de mesure via `--define DIAG_HEAP`,
  absents par défaut. **Ne pas activer en production** : l'endpoint de
  diagnostic `/ajax_diag` n'est pas authentifié.

Commandes et prérequis : [guide de build et de déploiement](tools/README.md)
et [guide des tests](tests/README.md).

---

## 🚀 Fonctionnalités principales

- 🔋 **Optimisation automatique de l’autoconsommation**
  - Mesure en temps réel de la puissance active importée/exportée.
  - Répartition intelligente des surplus photovoltaïques vers une ou plusieurs charges.
  - Réduction de l’énergie injectée sur le réseau à 0 W.

- ⚙️ **Mesures de puissance multi-sources**
  - Lecture directe du **compteur Linky** via **prise TIC**.
  - Mesure par méthode **UxI**, **UxIx2**, ou **UxIx3** à l’aide de sondes de courant.
  - Support des capteurs externes via **MQTT**, **Shelly EM**, etc.

- 🔌 **Pilotage des charges**
  - Sorties pour  **relais statiques (SSR)**.
  - Pilotage par **Triac** avec découpe sinusoïdale (suivi de phase secteur).
  - Modes d’actionnement configurables : tout ou rien, proportionnel, sinusoïdal, multi-sinus.

- 🌐 **Interface Web intégrée**
  - Visualisation en temps réel de la puissance, de la tension, du courant, de l’état des sorties.
  - Configuration des seuils, horaires, priorités, et paramètres réseau.
  - Historique et graphiques accessibles via navigateur.

- 🖥️ **Affichage local (selon version)**
  - Écran couleur 320×240  en option affichant les mesures instantanées et l’état des charges.
  - Navigation par boutons tactiles.

- 💬 **Communication et intégration domotique**
  - Serveur Web HTTP intégré.
  - Serveur Telnet (port 23 ) intégré.
  - **MQTT** pour l’intégration dans Home Assistant, Domoticz, Jeedom, etc.
  - **OTA (Over-The-Air)** : mise à jour logicielle à distance via WiFi ou Ethernet.
  - Configuration via  page Web embarquée.

- 🔒 **Sécurité et fiabilité**
  - **Watchdog** matériel/logiciel pour redémarrage automatique en cas de blocage.


- 🧩 **Compatibilité matérielle**
  - ESP32 Wroom,  WT32-ETH01, ESP32-ETH01, etc.
  - Connexion **WiFi** ou **Ethernet (PHY_LAN8720)**.
  - Alimentation possible via PoE (selon module).

---

## 📊 Exemple d’interface Web

![Interface Web](https://f1atb.fr/wp-content/uploads/2024/06/Capture-decran-2024-06-01-081356.png)

---

## ⚙️ Configuration typique

| Élément | Fonction | Interface |
|----------|-----------|------------|
| ESP32 | Microcontrôleur principal | WiFi / Ethernet |
| Linky TIC | Mesure puissance réseau | UART |
| Capteur UxI | Mesure courant + tension | ADC |
| Chauffe-eau | Charge 1 | Triac 1 |
| Chauffage | Charge 2 | Relais 2 |
| Capteur DS18B20 | Température | 1-Wire |
| Serveur MQTT | Communication domotique | TCP/IP |

---

## 🔧 Installation

> Compilation en ligne de commande, traçabilité des builds, flash et OTA : voir [tools/README.md](tools/README.md).

1. **Cloner le dépôt**
   ```bash
   git clone https://github.com/tsabran/Solar-Router-F1ATB.git
   cd Solar-Router-F1ATB
   ```

2. **Compiler et déployer**

   Suivre le [guide de build et de déploiement](tools/README.md) pour installer
   les prérequis, compiler et choisir une cible USB ou OTA. Sauvegarder la
   configuration du routeur avant toute mise à jour.
