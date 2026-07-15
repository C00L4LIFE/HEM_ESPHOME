# HEM_ESPHOME — Home Energy Management, version ESPHome

[![ESPHome](https://img.shields.io/badge/ESPHome-2026.6-blue)](https://esphome.io)
[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](LICENSE)

Dépôt : <https://github.com/C00L4LIFE/HEM_ESPHOME>

Portage ESPHome du projet C++ Home_Energy_Management (PlatformIO / Arduino).
Même matériel, mêmes pins, même logique de protection batterie — mais en
configuration YAML avec intégration Home Assistant native.

## Matériel (identique au projet d'origine)

| Périphérique | Bus | Pins ESP32 |
|---|---|---|
| MPPT eSmart3 | RS485 (9600 8N1) | RX 16 / TX 17 / DE 4 |
| 2× PZEM-004T v3 | Modbus RTU (9600) | RX 13 / TX 14 — adr. 1 (conso AC), adr. 2 (secteur) |
| JBD BMS (Xiaoxiang) | BLE | MAC dans `secrets.yaml` |
| RTC DS3231 | I2C | SDA 21 / SCL 22 |
| 4 relais (actifs bas) | GPIO | 25, 26, 27, 32 |
| 4 entrées | GPIO | 33 (pull-up), 34/35/36 (pull-up externe requis) |
| LED statut | GPIO | 2 |

Le JK BMS n'est **pas** géré par ce device : l'ESP32 classique n'a que 3 UART
matériels (logger + PZEM + eSmart3 les occupent déjà) et le JK BMS est déjà
suivi par un device ESP32 séparé (`jk-bms` / esp32-ble-v19-dual_bms.yaml).

Batterie JBD supposée 8S (8 cellules) : seules `cell_voltage_1` à `_8` sont
déclarées dans `packages/jbd_bms_ble.yaml` (au-delà, l'entité reste toujours
"Inconnu"). Décommenter `cell_voltage_9` à `_16` pour une batterie plus grande.

## Structure

```
home_energy_management.yaml   Fichier principal (pins, board, packages)
secrets.yaml                  Identifiants WiFi/MQTT, MAC JBD (non commité)
packages/
  wifi.yaml                   STA + AP secours + portail captif
  mqtt.yaml                   API native Home Assistant (MQTT en option)
  system.yaml                 OTA, web server, NTP + DS3231, watchdog MPPT
  gpio.yaml                   4 relais + 4 entrées
  pzem.yaml                   2× PZEM-004T v3 (pzemac natif)
  mppt_esmart3.yaml           MPPT eSmart3 (composant custom)
  jbd_bms_ble.yaml            JBD BMS BLE (syssi/esphome-jbd-bms)
  battery_protection.yaml     Protection batterie automatique
  energy.yaml                 Puissance maison, énergie batterie (kWh), temps restant estimé...
components/
  esmart3/                    Composant externe custom (protocole Joba_ESmart3)
```

## Composant custom `esmart3`

Aucun composant ESPHome n'existait pour l'eSmart3 : le protocole (trames
`0xAA`, CRC = négation de la somme, items ChgSts/BatParam/Log/ProParam/
LoadParam/Information) a été porté depuis la lib Joba_ESmart3 du projet
d'origine, en non-bloquant (machine à états dans `loop()`).

Expose : capteurs PV/batterie/load/énergies/défauts, mode de charge, état de
connexion, `switch` sortie load, et en écriture (`number`/`select`) :
- Courant de charge max, courant load max (`BatParam`)
- Tension Bulk/Float/Égalisation, durée d'égalisation, type de batterie
  (`select` Utilisateur/Plomb-acide/Gel/AGM) (`BatParam`)
- Seuils de protection Load OVP/UVP, Batterie OVP/UVP (+ récupération)
  (`ProParam`)

Chaque paramètre écriture est aussi disponible en `sensor` lecture seule
(suffixe "(lu)") pour historiser sa valeur sans dépendre de l'état d'un
`number`. Les écritures sont mises en file (une par transaction RS485) et
relues automatiquement après confirmation (ACK) pour rafraîchir l'affichage.

### Mise à l'échelle tension système (12/24/36/48V)

Le contrôleur stocke les tensions de config (Bulk/Float/Égalisation, Load et
Batterie OVP/UVP) en **base 12V**, quel que soit le système réel (un système
24V lit par ex. bulk=14.4 pour une tension réelle de 28.8V). Le `select`
**MPPT Tension système** (Auto/12V/24V/36V/48V) pilote un facteur d'échelle
appliqué automatiquement en lecture (multiplie) et en écriture (divise) sur
ces paramètres, pour que les `number`/`sensor` affichent et acceptent
toujours la tension réelle. En mode **Auto** (par défaut), le facteur est
déduit de la tension batterie mesurée (bandes 10-15V/20-30V/32-38V/40-60V) ;
le facteur retenu est visible via **MPPT Tension système détectée**. Ce
réglage est persistant (redémarrage).

## Capteurs d'énergie batterie

Le composant `jbd_bms_ble` ne fournit que des puissances instantanées
(`power`, `charging_power`, `discharging_power`), pas de compteur kWh natif.
`packages/energy.yaml` ajoute donc, via `sensor: platform: integration`
(intégration temporelle de la puissance) :
- **Batterie Énergie chargée** / **déchargée** (kWh, `total_increasing`,
  persistantes au redémarrage) — compatibles avec le tableau de bord
  **Énergie** de Home Assistant
- **Batterie Énergie restante** / **nominale** (Wh, capacité × tension)
- **Maison Énergie** (kWh, même principe, intégration de la puissance maison)
- **Batterie Signal BLE** (RSSI, via `ble_client`)

## Protection batterie

Portage fidèle de `battery_protection.cpp` (`packages/battery_protection.yaml`) :

1. **Limite absolue** de courant de charge
2. **Cellule en surtension** : pré-déclenchement 20 mV avant le seuil, consigne
   urgente (ex. 0 A), sortie uniquement si tension < seuil − hystérésis **et**
   delta cellules < seuil
3. **Équilibrage** : delta élevé + SoC ≥ seuil → courant réduit
4. **Batterie pleine** : SoC ≥ seuil → courant minimum
5. **Recharge auto** après décharge prolongée
+ courbe de charge adaptée au SoC (100 % → 70 % / 85 % / 95 %), moyenne
glissante du courant, cooldown 5 s entre actions, hystérésis 5 A.

Tous les seuils sont réglables depuis Home Assistant / le web server via les
entités `number` (persistées en NVS, comme l'espace `bat_protect` d'origine).
Interrupteur global **Protection batterie active**.

## Installation

`packages:` et `external_components:` pointent tous les trois directement
vers GitHub (même mécanisme que les composants syssi) : **seuls
`home_energy_management.yaml` et `secrets.yaml` sont nécessaires**, pas
besoin de cloner `packages/` ni `components/` à la main.

### Sur un poste (CLI / VS Code + ESPHome)

```bash
pip install esphome
mkdir hem && cd hem
curl -O https://raw.githubusercontent.com/C00L4LIFE/HEM_ESPHOME/main/home_energy_management.yaml
curl -O https://raw.githubusercontent.com/C00L4LIFE/HEM_ESPHOME/main/secrets.yaml.example
cp secrets.yaml.example secrets.yaml
# Renseigner secrets.yaml : WiFi, jbd_bms_mac_address, et générer une
# api_encryption_key unique (voir commentaire dans le fichier)
esphome run home_energy_management.yaml
```

(Cloner le dépôt entier fonctionne aussi et reste utile pour lire/modifier
les packages en local — voir la section suivante.)

### Dans l'add-on ESPHome de Home Assistant

Copier uniquement `home_energy_management.yaml` dans `/config/esphome/`,
puis dans le même dossier créer `secrets.yaml` (à partir de
[secrets.yaml.example](secrets.yaml.example)). Les packages, le composant
`esmart3` et les composants syssi sont récupérés automatiquement depuis
GitHub à la compilation — inutile de copier `packages/` ou `components/`.

Premier flash par USB, ensuite OTA.

### Mettre à jour après un push sur `main`

`refresh: 0s` sur `packages:` et `external_components:` : les sources
distantes sont **re-téléchargées à chaque compilation**, jamais de cache
périmé. Un simple "Installer"/`esphome run` suffit désormais pour récupérer
la dernière version poussée sur `main`.

Ce comportement est défini **dans `home_energy_management.yaml` lui-même**
(le `refresh:` de chaque source) — contrairement aux changements dans
`packages/*.yaml` ou `components/esmart3/`, une mise à jour de ce
paramètre-là nécessite donc de récupérer la dernière version de
`home_energy_management.yaml` (pas seulement relancer "Installer" sur
l'ancienne copie). À faire une seule fois : après ça, "Installer" suffit
toujours.

Si un device semble tourner sur une ancienne version malgré tout (ex. le
timestamp `compiled on ...` dans les logs ne change pas d'un flash à
l'autre), l'add-on ESPHome Device Builder de Home Assistant garde parfois
un cache de build indépendant de `refresh:`. Forcer un nettoyage complet :
menu **⋮** à côté du device → **Nettoyer les fichiers de compilation** (ou
équivalent), puis relancer "Installer". En CLI : `esphome clean
home_energy_management.yaml && esphome run home_energy_management.yaml`.

### Développer en local (modifier ce dépôt)

Pour tester une modification de `packages/*.yaml` ou de `components/esmart3`
**avant** de la pousser sur GitHub, cloner le dépôt et remplacer
temporairement, dans `home_energy_management.yaml`, le bloc `packages:` par
des `!include packages/xxx.yaml` locaux et la source `esmart3` par
`type: local, path: components` — sinon la config continue de compiler la
dernière version publiée sur `main`, pas vos fichiers locaux non poussés.

## Réutiliser le composant `esmart3` dans un autre projet

Le composant est utilisable directement depuis ce dépôt, sans cloner :

```yaml
external_components:
  - source: github://C00L4LIFE/HEM_ESPHOME@main
    components: [esmart3]

uart:
  - id: uart_esmart3
    rx_pin: 16
    tx_pin: 17
    baud_rate: 9600

esmart3:
  id: esmart3_hub
  uart_id: uart_esmart3
  flow_control_pin: 4   # DE/RE du transceiver RS485 (optionnel)
  update_interval: 1s
```

Voir [packages/mppt_esmart3.yaml](packages/mppt_esmart3.yaml) pour la liste
complète des capteurs, `number` et `switch` disponibles.

## Correspondance avec le projet d'origine

| Module C++ | Équivalent ESPHome |
|---|---|
| `wifi_manager.cpp` | `wifi:` + `ap:` + `captive_portal:` |
| `time_manager.cpp` | `time: sntp` + `ds1307` (write au sync NTP, read au boot) |
| `web_server.cpp` + HTML | `web_server:` v3 (interface générique ESPHome) |
| `ha_manager.cpp` | `api:` native (MQTT en option, voir mqtt.yaml) |
| `ota_manager.cpp` | `ota:` + `safe_mode:` |
| `mppt_manager.cpp` + Joba_ESmart3 | `components/esmart3` (custom) |
| `jk_bms.cpp` | non porté — géré par un device ESP32 séparé (`jk-bms`) |
| `ble_battery.cpp` | `syssi/esphome-jbd-bms` (BLE) |
| `pzem_manager.cpp` | `pzemac` natif ×2 |
| `gpio_manager.cpp` | `switch: gpio` ×4 + `binary_sensor: gpio` ×4 |
| `battery_protection.cpp` | `packages/battery_protection.yaml` |
| Watchdogs (`main.cpp`) | `reboot_timeout` WiFi/MQTT + interval watchdog MPPT |

## Limitations / différences

- **JK BMS** : non géré par ce device (voir plus haut) — l'ESP32 classique
  n'a que 3 UART matériels, déjà occupés par le logger, PZEM et eSmart3.
- **Température du DS3231** : non exposée (le composant `ds1307` ne la lit pas).
- **Historique eSmart3 31 j / 12 mois** (`EngSave`) : non porté — Home
  Assistant historise déjà les capteurs d'énergie.
- **Synchro heure du MPPT** : non portée (peu fiable, cf. commentaires de la
  lib Joba_ESmart3).
- **Interface web custom** (HTML/) : remplacée par le web server ESPHome.
- **Calibration courant de charge MPPT** : le facteur ×0.9 du projet d'origine
  est appliqué via un filtre sur le capteur `MPPT Courant de charge`.
- **Watchdog BLE** : non activé par défaut (le `ble_client` se reconnecte
  seul ; un reboot périodique bootloopait si la MAC n'est pas configurée).
- **Comptage d'équilibrage cellules** (`battery_balance_count` d'origine) :
  non porté en tant que compteur numérique — la liste des cellules en
  équilibrage reste disponible via le text_sensor "Batterie Cellules en
  équilibrage" (`jbd_bms_ble`).
- **Compteur agrégé de défauts batterie** (`battery_fault_count` d'origine) :
  non porté — chaque défaut reste disponible individuellement en
  `binary_sensor` (surtension/sous-tension cellule, surchauffe, surcourant,
  court-circuit).
- **État de connexion PZEM** (`ac_load_connected`/`grid_connected`
  d'origine) : non porté — le composant `pzemac` natif ne fournit pas de
  binary_sensor de connexion par adresse.
- **Énergie PZEM mensuelle/totale séparée** : `pzemac` n'expose qu'un
  compteur `energy` cumulé (contrairement aux registres MPPT internes qui
  distinguent jour/mois/total).
- **Reset des compteurs "Load" (consommation) via le bouton MPPT** :
  temporaire seulement. Testé sur le matériel : le SET écrit correctement
  0 sur `dwLoadTodayEng`/`dwLoadMonthEng`/`dwLoadTotalEng` (confirmé
  immédiatement après l'appui), mais le contrôleur les recalcule seul à
  `0x10000` (65536 Wh) une dizaine de secondes plus tard — probablement
  depuis un compteur interne que ce protocole ne permet pas de
  réinitialiser. Le reset des compteurs de **génération PV** (Today/Month/
  Total), lui, persiste normalement.

## Corrections notables

- **Température batterie/contrôleur MPPT** : les registres eSmart3
  `wBatTemp`/`wInnerTemp` sont déjà en °C entiers (pas de division par 10
  comme les tensions/courants du même message) — un firmware antérieur
  affichait 10× trop bas (ex. 3.4 °C au lieu de 34.0 °C).
- **Décodage modèle/série/firmware MPPT** : les paires de caractères ASCII
  étaient assemblées dans le mauvais ordre (octet fort avant l'octet
  faible), ce qui inversait chaque paire de lettres (ex. "Seamtr-306-APMTP"
  au lieu de "eSmart3-60A-MPPT").
- **Énergie MPPT mensuelle/totale (générée et consommée)** : confirmé dans
  la doc protocole officielle (`§7.1.3 Run log`) que `dwMonthEng`/
  `dwTotalEng`/`dwLoad*Eng` sont tous en **1 Wh** (valeur brute directe,
  aucune division) — le firmware d'origine divisait par 1000 en les
  qualifiant de kWh, une double erreur (calcul et libellé) reproduite puis
  seulement à moitié corrigée dans une itération précédente (libellé
  changé sans retirer la division, sous-évaluant le résultat de 1000×).
- **CO2 économisé** : `dwCO2` est documenté "0.1kg" (÷10), pas "0.001kg"
  (÷1000) — affichait 100× trop bas.

## Home Assistant

`api:` (native ESPHome) est actif par défaut : Home Assistant découvre le
device automatiquement (intégration ESPHome), sans dépendre d'un broker
MQTT tiers — c'est aussi ce canal qu'utilisent `esphome logs` et l'add-on
ESPHome pour streamer les logs. Nécessite `api_encryption_key` dans
`secrets.yaml` (voir `secrets.yaml.example`).

Si un autre système que Home Assistant doit consommer les topics MQTT
`hem/XXXX/...` (ex. Node-RED), décommentez le bloc `mqtt:` dans
`packages/mqtt.yaml` **en plus** de `api:` — passez alors `discovery: false`
côté MQTT pour éviter les entités HA en double. Utilisez une adresse IP ou
un hostname complet pour `mqtt_broker` : un nom court (ex. `"al"`) peut
échouer avec `Errno 22 Invalid argument` selon la résolution DNS du
conteneur qui exécute ESPHome.

## Licence

GPL-2.0 (voir [LICENSE](LICENSE)) — le composant `esmart3` est un portage de
la bibliothèque [Joba_ESmart3](https://github.com/joba-1/Joba_ESmart3) de
Joachim Banzhaf, publiée sous GPL V2.
