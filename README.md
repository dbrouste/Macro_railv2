# ESP32 Macro Rail Control (Hybrid WiFi + BLE)

Ce projet est un contrôleur firmware pour un rail macro motorisé basé sur un **ESP32**.
L'objectif est de piloter avec précision un moteur pas-à-pas pour la photographie macro, tout en contrôlant directement un boîtier **Sony** via WiFi Direct, le tout pilotable depuis une **interface web** hébergée localement et communicant via **Web Bluetooth**.

## 🏗️ Architecture

1. **Démarrage (Mode Setup)** : L'ESP32 démarre un réseau WiFi Access Point (`ESP32_Rail_Setup`) et un serveur Web HTTP local. Il démarre en parallèle un serveur BLE (`NimBLE`).
2. **Interface Utilisateur** : Le téléphone se connecte au WiFi de l'ESP32. L'interface Web est chargée depuis `http://192.168.4.1`.
3. **Contrôle BLE** : Depuis la page web, on initie une connexion Web Bluetooth vers l'ESP32. Toutes les commandes moteur et statuts passent désormais par cette connexion BLE rapide et basse latence.
4. **Connexion Appareil Photo (Sony)** : Lorsqu'on déclenche la connexion à l'appareil photo, l'ESP32 éteint proprement son WiFi AP/Serveur Web, et bascule en mode Client (STA) pour se connecter au réseau WiFi Direct du boîtier Sony. L'interface Web (déjà chargée sur le téléphone) reste active et continue de piloter l'ESP32 via le BLE resté connecté.

## 🛠️ Pré-requis et Dépendances

Le projet utilise **PlatformIO** et le framework Arduino.
* **Environnement** : `esp32doit-devkit-v1`
* **Système de fichiers** : `LittleFS`
* **Bibliothèques externes** : `h2zero/NimBLE-Arduino` (remplace le BluetoothSerial classique pour optimiser la mémoire et permettre la compatibilité iOS/WebBLE).

---

## 🚀 Protocole d'Installation (Important)

L'installation de ce firmware se fait en **deux étapes distinctes**. Le code C++ doit être flashé, mais la page Web (HTML/JS/CSS) stockée dans le dossier `data/` doit également être transférée dans la mémoire interne (LittleFS) de l'ESP32.

### Étape 1 : Téléversement du Code Firmware
1. Ouvrez le projet dans VS Code avec PlatformIO.
2. Cliquez sur l'icône de la fourmi PlatformIO 👽 dans la barre latérale gauche.
3. Allez dans `Project Tasks` > `esp32doit-devkit-v1` > `General`.
4. Cliquez sur **Upload**.

### Étape 2 : Téléversement du File System (LittleFS)
C'est ici que l'interface Web est placée dans la mémoire de l'ESP32.
1. Toujours dans la barre latérale PlatformIO (`Project Tasks` > `esp32doit-devkit-v1`).
2. Déroulez le menu **Platform**.
3. Cliquez sur **Upload Filesystem Image**.
*(Alternative en ligne de commande : `pio run --target uploadfs`)*

> [!WARNING]
> Si l'Étape 2 n'est pas réalisée, le réseau WiFi sera bien créé, mais la page `http://192.168.4.1` n'affichera rien car le fichier `index.html` sera introuvable.

---

## 📱 Utilisation

1. **Mise sous tension** : Allumez l'ESP32. Le système initialise le moteur et ouvre son réseau.
2. **Connexion WiFi** : Sur votre smartphone/PC, connectez-vous au réseau WiFi **`ESP32_Rail_Setup`**.
3. **Chargement de l'interface** : Ouvrez **Google Chrome** (ou un navigateur WebBLE sur iOS) et rendez-vous sur l'adresse : `http://192.168.4.1`.
4. **Appairage BLE** : Cliquez sur le bouton violet **"Connect BLE"** en haut de la page. Acceptez la demande d'association avec `ESP32_Rail`. Le statut passera au vert ("BLE Connected").
5. **Mode Prise de Vue** : Pilotez le moteur (Avance/Recule, Start, End). Cliquez sur **"Connect Sony"** : l'ESP32 bascule sur le WiFi de l'appareil photo. *Note: vous perdrez la connexion internet WiFi sur votre téléphone, mais la page web doit rester ouverte pour continuer le contrôle via BLE.*

## ⚙️ Hardware (Pins)

Les pins par défaut (configurables dans `main.cpp`) :
* `STEP` : GPIO 17
* `DIR` : GPIO 4
* `MS1` : GPIO 18
* `MS2` : GPIO 27
* `ENABLE` : GPIO 25
