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
| JK BMS | RS485 (115200, trames 0x4E 0x57) | RX 18 / TX 19 / DE 23 |
| 2× PZEM-004T v3 | Modbus RTU (9600) | RX 13 / TX 14 — adr. 1 (conso AC), adr. 2 (secteur) |
| JBD BMS (Xiaoxiang) | BLE | MAC dans `secrets.yaml` |
| RTC DS3231 | I2C | SDA 21 / SCL 22 |
| 4 relais (actifs bas) | GPIO | 25, 26, 27, 32 |
| 4 entrées | GPIO | 33 (pull-up), 34/35/36 (pull-up externe requis) |
| LED statut | GPIO | 2 |

## Structure

```
home_energy_management.yaml   Fichier principal (pins, board, packages)
secrets.yaml                  Identifiants WiFi/MQTT, MAC JBD (non commité)
packages/
  wifi.yaml                   STA + AP secours + portail captif
  mqtt.yaml                   MQTT discovery Home Assistant (ou API native)
  system.yaml                 OTA, web server, NTP + DS3231, watchdog MPPT
  gpio.yaml                   4 relais + 4 entrées
  pzem.yaml                   2× PZEM-004T v3 (pzemac natif)
  mppt_esmart3.yaml           MPPT eSmart3 (composant custom)
  jk_bms.yaml                 JK BMS RS485 (syssi/esphome-jk-bms)
  jbd_bms_ble.yaml            JBD BMS BLE (syssi/esphome-jbd-bms)
  battery_protection.yaml     Protection batterie automatique
  energy.yaml                 Puissance maison, temps restant estimé...
components/
  esmart3/                    Composant externe custom (protocole Joba_ESmart3)
```

## Composant custom `esmart3`

Aucun composant ESPHome n'existait pour l'eSmart3 : le protocole (trames
`0xAA`, CRC = négation de la somme, items ChgSts/BatParam/Log/ProParam/
LoadParam/Information) a été porté depuis la lib Joba_ESmart3 du projet
d'origine, en non-bloquant (machine à états dans `loop()`).

Expose : capteurs PV/batterie/load/énergies/défauts, mode de charge,
`number` **Courant charge max** (écrit `wMaxChgCurr`), `number` courant load
max, `switch` sortie load, état de connexion.

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

```bash
pip install esphome
git clone https://github.com/C00L4LIFE/HEM_ESPHOME.git
cd HEM_ESPHOME
cp secrets.yaml.example secrets.yaml
# Renseigner secrets.yaml (WiFi, MQTT et surtout jbd_bms_mac_address)
esphome run home_energy_management.yaml
```

Premier flash par USB, ensuite OTA. Les composants syssi sont téléchargés
automatiquement depuis GitHub à la compilation.

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
| `ha_manager.cpp` | `mqtt:` discovery (ou `api:` native, voir mqtt.yaml) |
| `ota_manager.cpp` | `ota:` + `safe_mode:` |
| `mppt_manager.cpp` + Joba_ESmart3 | `components/esmart3` (custom) |
| `jk_bms.cpp` | `syssi/esphome-jk-bms` (jk_modbus RS485) |
| `ble_battery.cpp` | `syssi/esphome-jbd-bms` (BLE) |
| `pzem_manager.cpp` | `pzemac` natif ×2 |
| `gpio_manager.cpp` | `switch: gpio` ×4 + `binary_sensor: gpio` ×4 |
| `battery_protection.cpp` | `packages/battery_protection.yaml` |
| Watchdogs (`main.cpp`) | `reboot_timeout` WiFi/MQTT + interval watchdog MPPT |

## Limitations / différences

- **JK BMS multi-unités** : le composant syssi ne gère qu'un seul BMS par bus
  RS485 (le projet d'origine supportait l'adressage multi-drop).
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

## Home Assistant

Avec `mqtt:` + discovery, toutes les entités apparaissent automatiquement.
Si vous préférez l'API native ESPHome (recommandé si tout passe par HA),
commentez `mqtt:` et décommentez `api:` dans `packages/mqtt.yaml` pour éviter
les entités en double.

## Licence

GPL-2.0 (voir [LICENSE](LICENSE)) — le composant `esmart3` est un portage de
la bibliothèque [Joba_ESmart3](https://github.com/joba-1/Joba_ESmart3) de
Joachim Banzhaf, publiée sous GPL V2.
