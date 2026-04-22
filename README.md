# Stazione Meteo IoT - ESP32
Firmware Fault-Tolerant per Stazione Meteo IoT con ESP32 e Firebase.

[![Language](https://img.shields.io/badge/Language-C++-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-ESP32-green.svg)](https://www.espressif.com/en/products/socs/esp32)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

*(🇬🇧 Scroll down for the English version)*

## 🇮🇹 Versione Italiana

### 📖 Cos'è questo progetto?
**Meteo IoT** è il firmware per una stazione meteorologica basata sul microcontrollore **ESP32**. Non si tratta di un semplice prototipo: il codice è stato ingegnerizzato per essere **Fault-Tolerant** (tollerante ai guasti) e operare 24/7 all'esterno senza blocchi.

Raccoglie dati ambientali e li trasmette al cloud (Google Firebase) utilizzando un'architettura ibrida, garantendo aggiornamenti in tempo reale per la dashboard web e un database storico pulito ed efficiente.

### ⚡ Caratteristiche Principali
* **Architettura Non-Bloccante:** Utilizzo esclusivo di `millis()` e interrupt hardware (`IRAM_ATTR`). Zero delay.
* **Fault-Tolerance (Debounce & Edge-Detection):** I sensori I2C e One-Wire sono protetti da falsi positivi. Gli errori vengono loggati istantaneamente solo al reale cambio di stato, dotati di Timestamp NTP.
* **Telemetria a Doppio Canale:**
  * *Realtime (2s):* Dati istantanei per interfacce web fluide.
  * *Storico (10m):* Dati aggregati. Utilizza trappole temporali per registrare la **Raffica Massima (Max Gust)** del vento e il **Volume di Pioggia** esatto nel periodo.
* **OTA (Over-The-Air) Sicuro:** Aggiornamenti firmware via Wi-Fi protetti da autenticazione HTTP tramite libreria `ElegantOTA`.
* **Diagnostica Locale:** Interfaccia utente su display TFT con pagine dedicate allo stato della rete e alla salute dei moduli I/O.

### 🛠️ Preparazione dell'IDE (Arduino IDE)
Per compilare correttamente questo firmware, è necessario preparare l'ambiente di sviluppo.

#### 1. Configurazione Scheda ESP32
1. Apri Arduino IDE, vai su *File > Impostazioni*.
2. In *URL aggiuntivi per il gestore schede*, inserisci: `https://dl.espressif.com/dl/package_esp32_index.json`
3. Vai su *Strumenti > Scheda > Gestore Schede*, cerca **esp32** e installa il pacchetto ufficiale di Espressif Systems.
4. Seleziona la scheda: **ESP32 Dev Module**.

#### 2. Installazione Librerie
Vai su *Sketch > Includi libreria > Gestione librerie* e installa esattamente queste versioni:
* `DHT sensor library` by Adafruit (e accetta l'installazione delle dipendenze come `Adafruit Unified Sensor`).
* `Adafruit BMP280 Library` by Adafruit.
* `Adafruit ST7735 and ST7789 Library` by Adafruit.
* `Adafruit GFX Library` by Adafruit.
* `Firebase Arduino Client Library for ESP8266 and ESP32` by Mobizt.
* `ElegantOTA` by Ayush Sharma.

#### 3. Configurazione Memoria (CRITICO ⚠️)
Poiché il codice utilizza crittografia SSL per Firebase e un WebServer per l'OTA, supera il limite di memoria standard.
* Vai su *Strumenti > Partition Scheme* e seleziona: **Minimal SPIFFS (1.9MB APP with OTA / 190KB SPIFFS)**.

### 🚀 Installazione e Avvio
1. Scarica o clona questo repository.
2. Rinomina il file `private_template.h` in **`private.h`**.
3. Apri il file `private.h` e inserisci le tue credenziali Wi-Fi, le chiavi API di Firebase e la password desiderata per l'accesso OTA. *(Nota: private.h è ignorato da git per sicurezza, non verrà mai caricato online).*
4. Collega l'ESP32 e clicca su **Carica**.
5. *Opzionale:* Consulta il **Manuale Tecnico (PDF)** per gli schemi di cablaggio completi.

---
---

## 🇬🇧 English Version

### 📖 What is this project?
**Meteo IoT** is a firmware for a weather station based on the **ESP32** microcontroller. This is not a simple prototype: the code has been engineered to be **Fault-Tolerant** and operate 24/7 outdoors without freezing.

It collects environmental data and transmits it to the cloud (Google Firebase) using a hybrid architecture, ensuring real-time updates for web dashboards and a clean, efficient historical database.

### ⚡ Key Features
* **Non-Blocking Architecture:** Exclusive use of `millis()` and hardware interrupts (`IRAM_ATTR`). Zero delays.
* **Fault-Tolerance (Debounce & Edge-Detection):** I2C and One-Wire sensors are protected from false positives. Errors are logged instantly only upon a real state change, stamped with NTP time.
* **Dual-Channel Telemetry:**
  * *Realtime (2s):* Instantaneous data for smooth web UI animations.
  * *History (10m):* Aggregated data. Uses temporal traps to record the **Max Gust** of wind and the exact **Rain Volume** within the period.
* **Secure OTA (Over-The-Air):** Wi-Fi firmware updates protected by HTTP authentication via the `ElegantOTA` library.
* **Local Diagnostics:** User interface on a TFT display with dedicated pages for network status and I/O module health.

### 🛠️ IDE Preparation (Arduino IDE)
To compile this firmware correctly, you need to set up your development environment.

#### 1. ESP32 Board Setup
1. Open Arduino IDE, go to *File > Preferences*.
2. In *Additional Boards Manager URLs*, enter: `https://dl.espressif.com/dl/package_esp32_index.json`
3. Go to *Tools > Board > Boards Manager*, search for **esp32**, and install the official package by Espressif Systems.
4. Select the board: **ESP32 Dev Module**.

#### 2. Library Installation
Go to *Sketch > Include Library > Manage Libraries* and install exactly these packages:
* `DHT sensor library` by Adafruit (accept the installation of dependencies like `Adafruit Unified Sensor`).
* `Adafruit BMP280 Library` by Adafruit.
* `Adafruit ST7735 and ST7789 Library` by Adafruit.
* `Adafruit GFX Library` by Adafruit.
* `Firebase Arduino Client Library for ESP8266 and ESP32` by Mobizt.
* `ElegantOTA` by Ayush Sharma.

#### 3. Memory Configuration (CRITICAL ⚠️)
Because the code uses SSL encryption for Firebase and a WebServer for OTA, it exceeds the standard memory limit.
* Go to *Tools > Partition Scheme* and select: **Minimal SPIFFS (1.9MB APP with OTA / 190KB SPIFFS)**.

### 🚀 Installation and Setup
1. Download or clone this repository.
2. Rename the `private_template.h` file to **`private.h`**.
3. Open `private.h` and fill in your Wi-Fi credentials, Firebase API keys, and your desired OTA access password. *(Note: private.h is git-ignored for security and will never be uploaded online).*
4. Connect your ESP32 and click **Upload**.
5. *Optional:* Check the **Technical Manual (PDF)** for complete wiring schematics.
