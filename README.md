# ⚡ Routeur Photovoltaïque F1ATB

Routeur photovoltaïque basé sur **ESP32**, permettant d’optimiser l’autoconsommation d’une installation solaire en redirigeant les surplus de production vers des charges (chauffe-eau, chauffage, pompe, etc.) au lieu de les injecter sur le réseau.

[**Détails Routeur F1ATB**](https://f1atb.fr/fr)

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

> Compilation en ligne de commande, traçabilité des builds, flash et OTA : voir [README_BUILD.md](README_BUILD.md).

1. **Cloner le dépôt**
   ```bash
   git clone https://github.com/F1ATB/Solar-Router-F1ATB.git
   cd Routeur-Photovoltaique-F1ATB
