/**
 * ============================================================================
 * PROGETTO: Stazione Meteo IoT PRO - ESP32
 * AUTORE:   Andrea Leone
 * VERSIONE: 2.0 (Fault-Tolerant Edge Edition)
 * LICENZA:  Copyright (c) Andrea Leone. Tutti i diritti riservati.
 * È vietata la copia, modifica o distribuzione non autorizzata.
 * ============================================================================
 */


#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include "DHT.h"
#include <Adafruit_GFX.h>    
#include <Adafruit_ST7735.h> 
#include <SPI.h>
#include <WiFi.h> 
#include <Firebase_ESP_Client.h>
#include <Preferences.h> 
#include <time.h>

#include "private.h"

// --- NUOVE LIBRERIE PER OTA (Aggiornamenti Wi-Fi) ---
#include <WebServer.h>
#include <ElegantOTA.h>

// --- PIN PULSANTI MENU ---
#define PIN_SU  26
#define PIN_GIU 13
#define PIN_OK  25

// --- PIN ANEMOMETRO E ANEMOSCOPIO ---
#define PIN_HALL        32
#define PIN_PLUVIOMETRO 27
#define PIN_NORD        33
#define PIN_EST         14
#define PIN_SUD         16
#define PIN_OVEST       17

const char* ssid = SECRET_WIFI_SSID;       
const char* password = SECRET_WIFI_PASS;    
#define API_KEY SECRET_FIREBASE_API_KEY
#define DATABASE_URL SECRET_FIREBASE_URL
#define USER_EMAIL SECRET_USER_EMAIL
#define USER_PASSWORD SECRET_USER_PASS
const char* ota_user = SECRET_OTA_USER;
const char* ota_pass = SECRET_OTA_PASS;

FirebaseData fbdo;         
FirebaseData fbdoComandi;  
FirebaseAuth auth;
FirebaseConfig config;
Preferences preferenze; 

// --- SERVER WEB (PER OTA) ---
WebServer server(80);

// --- PIN SENSORI E SCHERMO ---
#define DHTPIN 4        
#define DHTTYPE DHT22   

const float altitude = 470;

#define TFT_CS     5
#define TFT_RST    15  
#define TFT_DC     2

DHT dht(DHTPIN, DHTTYPE);
Adafruit_BMP280 bmp; 
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- VARIABILI GLOBALI E TIMER ---
float umidita = 0.0, temp_dht = 0.0, pressione = 0.0, altitudine = 0.0;
float velocita_kmh = 0.0; 
float raffica_max = 0.0;
String direzione_vento = "---"; 

// --- NUOVE VARIABILI DI STATO SENSORI ED EDGE-DETECTION ---
bool stato_dht_ok = false, ultimo_stato_dht = true;
bool stato_bmp_ok = false, ultimo_stato_bmp = true;
bool stato_anemoscopio_ok = false, ultimo_stato_anemoscopio = true;
int errori_dht = 0;
int errori_bmp = 0;
// ------------------------------------------------------

unsigned long tempoSchermo = 0, tempoPing = 0, tempoFirebase = 0, tempoComandi = 0, ultimoTentativoWiFi = 0; 
unsigned long tempoPrecedenteVento = 0, tempoPrecedenteDirezione = 0, tempoRealtime = 0;
unsigned long ultimoClickPulsante = 0; // Timer anti-rimbalzo (Debounce)

// --- FISICA DELL'ANEMOMETRO E PLUVIOMETRO ---
volatile unsigned long contatoreImpulsi = 0; 
const float raggio_metri = 0.08;                           
const float circonferenza = 2.0 * PI * raggio_metri;       
const float fattore_taratura = 1.18;                       

volatile unsigned long scattiPioggia = 0;
volatile unsigned long tempoUltimoScatto = 0;
const float millimetri_per_scatto = 0.173; 

float pioggia_1h = 0.0, pioggia_24h = 0.0;
float pioggia_10min = 0.0;
int ultima_ora_registrata = -1, ultimo_giorno_registrato = -1;
unsigned long tempoUltimaGoccia = 0;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600; 
const int   daylightOffset_sec = 3600; 

volatile unsigned long tempoUltimoGiro = 0;

// --- INTERRUPT ---
void IRAM_ATTR contaGiri() {
  unsigned long tempoAttuale = millis();
  if (tempoAttuale - tempoUltimoGiro > 5) {
    contatoreImpulsi++;
    tempoUltimoGiro = tempoAttuale;
  }
}

void IRAM_ATTR contaPioggia() {
  unsigned long tempoAttuale = millis();
  if (tempoAttuale - tempoUltimoScatto > 500) {
    if (digitalRead(PIN_PLUVIOMETRO) == LOW) {
      scattiPioggia++;
      tempoUltimoScatto = tempoAttuale;
    }
  }
}

// --- MEMORIA LOG E GRAFICI ---
const int maxLogLocali = 15; 
String logLocali[maxLogLocali];
int indiceLog = 0, numeroLogSalvati = 0, scrollLogOffset = 0; 

const int maxPuntiGrafico = 20;
float storicoTempGrafico[maxPuntiGrafico];
float storicoVentoGrafico[maxPuntiGrafico]; 
int indiceGrafico = 0, numeroPuntiGrafico = 0;

// --- MENU ---
enum StatiMenu { DASHBOARD, MENU_LISTA, PAGINA_RETE, PAGINA_DIAGNOSTICA, PAGINA_LOG };
StatiMenu statoAttuale = DASHBOARD;
int paginaDashboard = 0; 
const int totPagineDashboard = 4; 
int cursoreMenu = 0;
const int totaleVoci = 4; 
String vociMenu[] = {"1. Dashboard", "2. Stato Rete", "3. Diagnostica", "4. Ultimi Log"};

// ==========================================
// FUNZIONI DI SUPPORTO (LOGICA)
// ==========================================

String getOrarioLog() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return ""; // Se l'orologio non è sincronizzato, non mette il prefisso
  }
  char timeStringBuff[15];
  strftime(timeStringBuff, sizeof(timeStringBuff), "[%H:%M:%S] ", &timeinfo);
  return String(timeStringBuff);
}

void aggiornaOrarioPioggia() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return; 
  
  int ora_attuale = timeinfo.tm_hour;
  int giorno_attuale = timeinfo.tm_mday;

  if (ultima_ora_registrata != -1 && ora_attuale != ultima_ora_registrata) pioggia_1h = 0.0;
  ultima_ora_registrata = ora_attuale;

  if (ultimo_giorno_registrato != -1 && giorno_attuale != ultimo_giorno_registrato) pioggia_24h = 0.0;
  ultimo_giorno_registrato = giorno_attuale;
}

void caricaLogSalvati() {
  preferenze.begin("meteo_logs", false);
  numeroLogSalvati = preferenze.getInt("num", 0);
  indiceLog = preferenze.getInt("idx", 0);
  for(int i=0; i<maxLogLocali; i++) {
    logLocali[i] = preferenze.getString(("log" + String(i)).c_str(), "");
  }
}

void inviaLog(String categoria, String messaggio) {
  // Aggiunge il timestamp all'inizio
  String logCompleto = getOrarioLog() + categoria + " " + messaggio;
  logLocali[indiceLog] = logCompleto;
  preferenze.putString(("log" + String(indiceLog)).c_str(), logCompleto);
  indiceLog = (indiceLog + 1) % maxLogLocali; 
  if (numeroLogSalvati < maxLogLocali) numeroLogSalvati++;
  preferenze.putInt("num", numeroLogSalvati);
  preferenze.putInt("idx", indiceLog);

  if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
    FirebaseJson logJson;
    logJson.set("messaggio", logCompleto);
    logJson.set("timestamp/.sv", "timestamp");
    Firebase.RTDB.pushJSON(&fbdo, "/meteo/log", &logJson);
  }
}

void aggiungiPuntoGrafico(float temp, float vento) {
  storicoTempGrafico[indiceGrafico] = temp;
  storicoVentoGrafico[indiceGrafico] = vento;
  indiceGrafico = (indiceGrafico + 1) % maxPuntiGrafico;
  if (numeroPuntiGrafico < maxPuntiGrafico) numeroPuntiGrafico++;
}

// ==========================================
// FUNZIONI GRAFICHE DISPLAY 
// ==========================================
void disegnaSegnaleWiFi(int x, int y) {
  tft.fillRect(x, y, 25, 15, ST77XX_BLACK); 
  if (WiFi.status() != WL_CONNECTED) {
    tft.setCursor(x, y + 2); tft.setTextColor(ST77XX_RED, ST77XX_BLACK); tft.print("ERR"); 
    return; 
  }
  int rssi = WiFi.RSSI(), tacche = 0;
  if (rssi > -55) tacche = 4; else if (rssi > -65) tacche = 3; else if (rssi > -75) tacche = 2; else if (rssi > -85) tacche = 1;
  for (int i = 0; i < 4; i++) {
    int altezza = 3 + (i * 3), x_tacca = x + (i * 5), y_tacca = y + 12 - altezza;     
    if (i < tacche) tft.fillRect(x_tacca, y_tacca, 3, altezza, ST77XX_WHITE); 
    else tft.fillRect(x_tacca, y_tacca, 3, altezza, tft.color565(80, 80, 80));
  }
}

void disegnaImpalcaturaDashboard() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(0, 0, 160, 128, ST77XX_WHITE); 
  tft.setCursor(10, 10); tft.setTextColor(ST77XX_CYAN); 
  if (paginaDashboard == 0) {
    tft.print(" METEO BASE (1/4) ");
    tft.drawLine(10, 22, 150, 22, ST77XX_CYAN); 
    tft.setTextColor(ST77XX_YELLOW); tft.setCursor(10, 35); tft.print("Temp Aria:");
    tft.setTextColor(ST77XX_GREEN);  tft.setCursor(10, 55); tft.print("Umidita':");
    tft.setTextColor(ST77XX_MAGENTA);tft.setCursor(10, 75); tft.print("Pressione:");
    tft.setTextColor(ST77XX_ORANGE); tft.setCursor(10, 95); tft.print("Altitud.:");
  } 
  else if (paginaDashboard == 1) {
    tft.print(" VENTO PIOGGIA(2/4)");
    tft.drawLine(10, 22, 150, 22, ST77XX_CYAN); 
    tft.setTextColor(ST77XX_WHITE);  tft.setCursor(10, 35); tft.print("Vel. Vento:");
    tft.setTextColor(ST77XX_YELLOW); tft.setCursor(10, 55); tft.print("Dir. Vento:");
    tft.setTextColor(ST77XX_BLUE);   tft.setCursor(10, 75); tft.print("Pioggia 1h:");
    tft.setTextColor(ST77XX_CYAN);   tft.setCursor(10, 95); tft.print("Pioggia 24h:");
  }
  else if (paginaDashboard == 2) {
    tft.print(" STORICO TEMP (3/4) ");
    tft.drawLine(10, 22, 150, 22, ST77XX_CYAN); 
  }
  else if (paginaDashboard == 3) {
    tft.print(" STORICO VENTO(4/4)");
    tft.drawLine(10, 22, 150, 22, ST77XX_CYAN); 
  }
  tft.fillRect(0, 115, 160, 13, ST77XX_BLACK);
  tft.setCursor(5, 118); tft.setTextColor(tft.color565(150, 150, 150)); tft.print("v/^ Sfoglia  OK: Menu");
  disegnaSegnaleWiFi(130, 4);
}

void aggiornaValoriDashboard() {
  tft.setTextSize(1);
  if (paginaDashboard == 0) {
    if (!stato_dht_ok) {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK); tft.setCursor(85, 35); tft.print("OFF       "); tft.setCursor(85, 55); tft.print("OFF       ");
    } else {
      tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK); tft.setCursor(85, 35); tft.print(temp_dht, 1); tft.print(" C  ");      
      tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);  tft.setCursor(85, 55); tft.print(umidita, 1); tft.print(" %  ");
    }
    
    if (!stato_bmp_ok) {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK); tft.setCursor(85, 75); tft.print("OFF       "); tft.setCursor(85, 95); tft.print("OFF       ");
    } else {
      tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK); tft.setCursor(85, 75); tft.print(pressione, 1); tft.print(" hPa");
      tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);  tft.setCursor(85, 95); tft.print(altitudine, 0); tft.print(" m   ");
    }
  } 
  else if (paginaDashboard == 1) {
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);  tft.setCursor(85, 35); tft.print(velocita_kmh, 1); tft.print(" km/h  ");
    
    if (!stato_anemoscopio_ok) {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK); tft.setCursor(85, 55); tft.print("OFF       ");
    } else {
      tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK); tft.setCursor(85, 55); tft.print(direzione_vento); tft.print("      ");
    }
    
    tft.setTextColor(ST77XX_BLUE, ST77XX_BLACK);   tft.setCursor(85, 75); tft.print(pioggia_1h, 1); tft.print(" mm  ");
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);   tft.setCursor(85, 95); tft.print(pioggia_24h, 1); tft.print(" mm  ");
  }
  else if (paginaDashboard == 2) {
    tft.fillRect(10, 30, 140, 80, ST77XX_BLACK);
    tft.drawRect(10, 30, 140, 80, tft.color565(50, 50, 50)); 
    if (numeroPuntiGrafico < 2) { tft.setCursor(25, 65); tft.setTextColor(ST77XX_WHITE); tft.print("In attesa dati..."); return; }
    float minT = storicoTempGrafico[0], maxT = storicoTempGrafico[0];
    for (int i = 0; i < numeroPuntiGrafico; i++) {
      if (storicoTempGrafico[i] < minT) minT = storicoTempGrafico[i];
      if (storicoTempGrafico[i] > maxT) maxT = storicoTempGrafico[i];
    }
    if (maxT - minT < 2.0) { minT -= 1.0; maxT += 1.0; } 
    int startIdx = (numeroPuntiGrafico == maxPuntiGrafico) ? indiceGrafico : 0;
    int prevX = -1, prevY = -1;
    for (int i = 0; i < numeroPuntiGrafico; i++) {
      int realeIdx = (startIdx + i) % maxPuntiGrafico;
      float t = storicoTempGrafico[realeIdx];
      int x = 10 + (i * (140 / (numeroPuntiGrafico > 1 ? numeroPuntiGrafico - 1 : 1)));
      int y = 105 - ((t - minT) * 70 / (maxT - minT)); 
      if (prevX != -1) tft.drawLine(prevX, prevY, x, y, ST77XX_YELLOW); 
      tft.fillCircle(x, y, 2, ST77XX_RED); 
      prevX = x; prevY = y;
    }
  }
  else if (paginaDashboard == 3) {
    tft.fillRect(10, 30, 140, 80, ST77XX_BLACK);
    tft.drawRect(10, 30, 140, 80, tft.color565(50, 50, 50)); 
    if (numeroPuntiGrafico < 2) { tft.setCursor(25, 65); tft.setTextColor(ST77XX_WHITE); tft.print("In attesa dati..."); return; }
    
    float minV = 0.0; 
    float maxV = 1.0; 
    for (int i = 0; i < numeroPuntiGrafico; i++) {
      if (storicoVentoGrafico[i] > maxV) maxV = storicoVentoGrafico[i];
    }
    
    int startIdx = (numeroPuntiGrafico == maxPuntiGrafico) ? indiceGrafico : 0;
    int prevX = -1, prevY = -1;
    for (int i = 0; i < numeroPuntiGrafico; i++) {
      int realeIdx = (startIdx + i) % maxPuntiGrafico;
      float v = storicoVentoGrafico[realeIdx];
      int x = 10 + (i * (140 / (numeroPuntiGrafico > 1 ? numeroPuntiGrafico - 1 : 1)));
      int y = 105 - ((v - minV) * 70 / (maxV - minV)); 
      
      if (prevX != -1) tft.drawLine(prevX, prevY, x, y, ST77XX_CYAN); 
      tft.fillCircle(x, y, 2, ST77XX_WHITE); 
      prevX = x; prevY = y;
    }
  }
}

void disegnaMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 10); tft.setTextColor(ST77XX_CYAN); tft.println("--- MENU SISTEMA ---");
  tft.drawLine(0, 22, 160, 22, ST77XX_WHITE);
  for (int i = 0; i < totaleVoci; i++) {
    int yPos = 35 + (i * 20);
    if (i == cursoreMenu) { tft.fillRect(0, yPos - 5, 160, 18, ST77XX_BLUE); tft.setTextColor(ST77XX_WHITE); } 
    else { tft.setTextColor(ST77XX_YELLOW); }
    tft.setCursor(15, yPos); tft.print(vociMenu[i]);
  }
}

void disegnaPaginaRete() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 10); tft.setTextColor(ST77XX_CYAN); tft.println("STATO RETE WI-FI");
  tft.drawLine(0, 22, 160, 22, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 35); tft.print("IP: "); tft.println(WiFi.localIP());
  tft.setCursor(10, 55); tft.print("Segnale: "); tft.print(WiFi.RSSI()); tft.println(" dBm");
  tft.setCursor(10, 75); tft.print("Firebase: "); 
  if (Firebase.ready()) { tft.setTextColor(ST77XX_GREEN); tft.println("CONNESSO"); } else { tft.setTextColor(ST77XX_RED); tft.println("DISCONNESSO"); }
  tft.setCursor(10, 110); tft.setTextColor(ST77XX_CYAN); tft.println("< PREMI OK PER MENU");
}

void disegnaPaginaDiagnostica() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 5); tft.setTextColor(ST77XX_CYAN); tft.println("DIAGNOSTICA MODULI");
  tft.drawLine(0, 15, 160, 15, ST77XX_WHITE);
  
  tft.setCursor(5, 25); tft.setTextColor(ST77XX_WHITE); tft.print("Rete Dati: ");
  if (Firebase.ready()) { tft.setTextColor(ST77XX_GREEN); tft.println("OK"); }
  else { tft.setTextColor(ST77XX_RED); tft.println("OFF"); }

  tft.setCursor(5, 40); tft.setTextColor(ST77XX_WHITE); tft.print("DHT22 (Aria): ");
  if (stato_dht_ok) { tft.setTextColor(ST77XX_GREEN); tft.println("OK"); }
  else { tft.setTextColor(ST77XX_RED); tft.println("OFF"); }

  tft.setCursor(5, 55); tft.setTextColor(ST77XX_WHITE); tft.print("BMP280 (Press): ");
  if (stato_bmp_ok) { tft.setTextColor(ST77XX_GREEN); tft.println("OK"); }
  else { tft.setTextColor(ST77XX_RED); tft.println("OFF"); }

  tft.setCursor(5, 70); tft.setTextColor(ST77XX_WHITE); tft.print("Dir. Vento: ");
  if (stato_anemoscopio_ok) { tft.setTextColor(ST77XX_GREEN); tft.println("OK"); }
  else { tft.setTextColor(ST77XX_RED); tft.println("OFF"); }

  tft.setCursor(5, 85); tft.setTextColor(ST77XX_WHITE); tft.print("Anem/Pluv: ");
  tft.setTextColor(ST77XX_YELLOW); tft.println("ATTIVI");

  tft.setCursor(10, 110); tft.setTextColor(ST77XX_CYAN); tft.println("< PREMI OK PER MENU");
}

void disegnaPaginaLog() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 5); tft.setTextColor(ST77XX_CYAN); tft.println("ULTIMI EVENTI (LOG)");
  tft.drawLine(0, 15, 160, 15, ST77XX_WHITE);
  
  int yPos = 20;
  if (numeroLogSalvati == 0) { 
    tft.setCursor(5, yPos); tft.setTextColor(ST77XX_WHITE); tft.print("Nessun log salvato."); 
  } else {
    int logVisibili = 4; 
    int inizio = numeroLogSalvati - logVisibili - scrollLogOffset;
    if(inizio < 0) inizio = 0;
    
    for (int i = inizio; i < numeroLogSalvati && yPos < 100; i++) {
      int idx = (indiceLog - numeroLogSalvati + i + maxLogLocali) % maxLogLocali;
      tft.setCursor(0, yPos);
      if (logLocali[idx].indexOf("ERRORE") >= 0 || logLocali[idx].indexOf("ALLARME") >= 0) tft.setTextColor(ST77XX_RED);
      else if (logLocali[idx].indexOf("SUCCESS") >= 0) tft.setTextColor(ST77XX_GREEN);
      else tft.setTextColor(ST77XX_YELLOW);
      
      tft.println(logLocali[idx]);
      yPos = tft.getCursorY() + 4; 
    }
  }
  
  tft.fillRect(0, 115, 160, 13, ST77XX_BLACK); 
  tft.setCursor(0, 118); tft.setTextColor(tft.color565(150, 150, 150)); 
  tft.print("v/^ Scorri  OK: Esci");
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_SU, INPUT_PULLUP);
  pinMode(PIN_GIU, INPUT_PULLUP);
  pinMode(PIN_OK, INPUT_PULLUP);

  pinMode(PIN_HALL, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(PIN_HALL), contaGiri, FALLING);
  pinMode(PIN_PLUVIOMETRO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_PLUVIOMETRO), contaPioggia, FALLING);

  pinMode(PIN_NORD, INPUT_PULLUP); pinMode(PIN_EST, INPUT_PULLUP);
  pinMode(PIN_SUD, INPUT_PULLUP); pinMode(PIN_OVEST, INPUT_PULLUP);

  caricaLogSalvati(); 

  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(1);        
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1);
  tft.println("Connessione Wi-Fi...");
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  server.on("/", []() {
    server.send(200, "text/plain", "Stazione Meteo Attiva. Vai a /update per caricare il nuovo firmware OTA.");
  });
  ElegantOTA.begin(&server, ota_user, ota_pass);
  server.begin();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  fbdo.setBSSLBufferSize(4096, 1024);
  fbdoComandi.setBSSLBufferSize(4096, 1024); 
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Attende l'orologio NTP per un log perfetto al riavvio
  delay(1000);
  inviaLog("[SISTEMA]", "INFO: Avvio stazione meteo.");
  
  tft.fillScreen(ST77XX_BLACK); tft.setCursor(10, 50); tft.println("Avvio sensori...");
  dht.begin();
  if (!bmp.begin(0x77)) bmp.begin(0x76);
  
  temp_dht = dht.readTemperature();
  umidita = dht.readHumidity();
  if(!isnan(temp_dht) && !isnan(umidita)) { 
    stato_dht_ok = true; 
    aggiungiPuntoGrafico(temp_dht, 0.0); 
  }

  disegnaImpalcaturaDashboard();
}

// ==========================================
// LOOP PRINCIPALE
// ==========================================
void loop() {
  unsigned long tempoAttuale = millis();

  server.handleClient();
  ElegantOTA.loop();

  if (WiFi.status() != WL_CONNECTED) {
    if (tempoAttuale - ultimoTentativoWiFi > 15000) { 
      WiFi.reconnect(); 
      ultimoTentativoWiFi = tempoAttuale;
    }
  } else {
    ultimoTentativoWiFi = tempoAttuale; 
  }

  // --- 1. GESTIONE PULSANTI (SENZA DELAY) ---
  if (tempoAttuale - ultimoClickPulsante > 300) { 
    if (digitalRead(PIN_SU) == LOW) {
      ultimoClickPulsante = tempoAttuale;
      if (statoAttuale == MENU_LISTA) { cursoreMenu = (cursoreMenu - 1 + totaleVoci) % totaleVoci; disegnaMenu(); } 
      else if (statoAttuale == DASHBOARD) { paginaDashboard = (paginaDashboard - 1 + totPagineDashboard) % totPagineDashboard; disegnaImpalcaturaDashboard(); aggiornaValoriDashboard(); } 
      else if (statoAttuale == PAGINA_LOG) { if (scrollLogOffset < numeroLogSalvati - 1) { scrollLogOffset++; disegnaPaginaLog(); } }
    }
    else if (digitalRead(PIN_GIU) == LOW) {
      ultimoClickPulsante = tempoAttuale;
      if (statoAttuale == MENU_LISTA) { cursoreMenu = (cursoreMenu + 1) % totaleVoci; disegnaMenu(); } 
      else if (statoAttuale == DASHBOARD) { paginaDashboard = (paginaDashboard + 1) % totPagineDashboard; disegnaImpalcaturaDashboard(); aggiornaValoriDashboard(); } 
      else if (statoAttuale == PAGINA_LOG) { if (scrollLogOffset > 0) { scrollLogOffset--; disegnaPaginaLog(); } }
    }
    else if (digitalRead(PIN_OK) == LOW) {
      ultimoClickPulsante = tempoAttuale;
      if (statoAttuale == DASHBOARD) { statoAttuale = MENU_LISTA; cursoreMenu = 0; disegnaMenu(); } 
      else if (statoAttuale == MENU_LISTA) {
        if (cursoreMenu == 0) { statoAttuale = DASHBOARD; disegnaImpalcaturaDashboard(); aggiornaValoriDashboard(); }
        else if (cursoreMenu == 1) { statoAttuale = PAGINA_RETE; disegnaPaginaRete(); }
        else if (cursoreMenu == 2) { statoAttuale = PAGINA_DIAGNOSTICA; disegnaPaginaDiagnostica(); }
        else if (cursoreMenu == 3) { statoAttuale = PAGINA_LOG; scrollLogOffset = 0; disegnaPaginaLog(); }
      } else { statoAttuale = MENU_LISTA; disegnaMenu(); }
    }
  }

  // --- 2. LETTURA SENSORI I2C (Ogni 2 Sec) CON EDGE DETECTION ---
  if (tempoAttuale - tempoSchermo >= 2000) {
    tempoSchermo = tempoAttuale;
    
    // DHT22
    float nuova_temp = dht.readTemperature();
    float nuova_umid = dht.readHumidity();
    if (isnan(nuova_temp) || isnan(nuova_umid)) {
      errori_dht++;
      if (errori_dht >= 3) stato_dht_ok = false;
    } else {
      errori_dht = 0;
      stato_dht_ok = true;
      temp_dht = nuova_temp;
      umidita = nuova_umid;
    }

    // EVENTO: Cambio di stato DHT22
    if (stato_dht_ok != ultimo_stato_dht) {
      if (!stato_dht_ok) inviaLog("[HARDWARE]", "ERRORE: DHT22 scollegato o guasto.");
      else inviaLog("[HARDWARE]", "SUCCESS: Sensore DHT22 ripristinato.");
      ultimo_stato_dht = stato_dht_ok;
    }

    // BMP280
    float nuova_press_abs = bmp.readPressure();
    if (nuova_press_abs <= 0 || isnan(nuova_press_abs)) {
      errori_bmp++;
      if (errori_bmp >= 3) stato_bmp_ok = false;
    } else {
      errori_bmp = 0;
      stato_bmp_ok = true;
      altitudine = altitude; 
      pressione = bmp.seaLevelForAltitude(altitude, nuova_press_abs) / 100.0F; 
    }

    // EVENTO: Cambio di stato BMP280
    if (stato_bmp_ok != ultimo_stato_bmp) {
      if (!stato_bmp_ok) inviaLog("[HARDWARE]", "ERRORE: BMP280 non rilevato su I2C.");
      else inviaLog("[HARDWARE]", "SUCCESS: Sensore BMP280 ripristinato.");
      ultimo_stato_bmp = stato_bmp_ok;
    }
    
    if (statoAttuale == DASHBOARD) { disegnaSegnaleWiFi(130, 4); aggiornaValoriDashboard(); }
    else if (statoAttuale == PAGINA_DIAGNOSTICA) { disegnaPaginaDiagnostica(); }
  }

  // --- 2.5 CALCOLO VELOCITA' E DIREZIONE VENTO ---
  if (tempoAttuale - tempoPrecedenteVento >= 3000) {
    tempoPrecedenteVento = tempoAttuale;
    noInterrupts(); unsigned long impulsi = contatoreImpulsi; contatoreImpulsi = 0; interrupts();
    float rotazioniAlSecondo = impulsi / 3.0;
    velocita_kmh = rotazioniAlSecondo * circonferenza * fattore_taratura * 3.6;

    if (velocita_kmh > raffica_max) {
      raffica_max = velocita_kmh;
    }

    if (statoAttuale == DASHBOARD && paginaDashboard == 1) aggiornaValoriDashboard(); 
  }

  if (tempoAttuale - tempoPrecedenteDirezione >= 2000) {
    tempoPrecedenteDirezione = tempoAttuale;
    bool n = !digitalRead(PIN_NORD); bool e = !digitalRead(PIN_EST); bool s = !digitalRead(PIN_SUD); bool o = !digitalRead(PIN_OVEST);
    
    if (!n && !e && !s && !o) {
      stato_anemoscopio_ok = false;
    } else {
      stato_anemoscopio_ok = true;
      String dir = "";
      if (n && !e && !o) dir = "NORD"; else if (n && e) dir = "N-EST"; else if (e && !n && !s) dir = "EST";
      else if (s && e) dir = "S-EST"; else if (s && !e && !o) dir = "SUD"; else if (s && o) dir = "S-OVEST";
      else if (o && !n && !s) dir = "OVEST"; else if (n && o) dir = "N-OVEST";

      if (dir != "" && direzione_vento != dir) {
        direzione_vento = dir;
      }
    }

    // EVENTO: Cambio di stato Anemoscopio
    if (stato_anemoscopio_ok != ultimo_stato_anemoscopio) {
      if (!stato_anemoscopio_ok) inviaLog("[HARDWARE]", "ERRORE: Cavo direzione vento interrotto.");
      else inviaLog("[HARDWARE]", "SUCCESS: Cavo direzione vento ripristinato.");
      ultimo_stato_anemoscopio = stato_anemoscopio_ok;
    }
    
    if (statoAttuale == DASHBOARD && paginaDashboard == 1) aggiornaValoriDashboard(); 
    else if (statoAttuale == PAGINA_DIAGNOSTICA) disegnaPaginaDiagnostica();
  }

  // --- 2.7 CALCOLO PIOGGIA ---
  static unsigned long vecchiScattiPioggia = 0;
  noInterrupts(); unsigned long scattiAttualiPioggia = scattiPioggia; interrupts();

  if (scattiAttualiPioggia != vecchiScattiPioggia) {
    float pioggia_aggiunta = (scattiAttualiPioggia - vecchiScattiPioggia) * millimetri_per_scatto;
    pioggia_1h += pioggia_aggiunta; 
    pioggia_24h += pioggia_aggiunta;
    pioggia_10min += pioggia_aggiunta;
    
    vecchiScattiPioggia = scattiAttualiPioggia;
    tempoUltimaGoccia = tempoAttuale;
    if (statoAttuale == DASHBOARD && paginaDashboard == 1) aggiornaValoriDashboard(); 
  }
  aggiornaOrarioPioggia();

  // --- 3. BATTITO CARDIACO SILENZIOSO (Ogni 30 Sec) ---
  if (tempoAttuale - tempoPing >= 30000) {
    tempoPing = tempoAttuale;
    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      Firebase.RTDB.setTimestamp(&fbdo, "/meteo/stato/ultimo_ping");
      
      // La dashboard riceve gli stati periodici
      Firebase.RTDB.setString(&fbdo, "/meteo/stato/sensori/dht", stato_dht_ok ? "OK" : "OFF");
      Firebase.RTDB.setString(&fbdo, "/meteo/stato/sensori/bmp", stato_bmp_ok ? "OK" : "OFF");
      Firebase.RTDB.setString(&fbdo, "/meteo/stato/sensori/anemoscopio", stato_anemoscopio_ok ? "OK" : "OFF");
    }
  }

  // --- 4. TERMINALE REMOTO (Ogni 3 Sec) ---
  if (tempoAttuale - tempoComandi >= 3000) {
    tempoComandi = tempoAttuale;
    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      
      if (Firebase.RTDB.getString(&fbdoComandi, "/meteo/comandi/azione")) {
        String comando = fbdoComandi.stringData();
        comando.replace("\"", ""); 
        comando.trim();
        comando.toLowerCase(); 
        
        if (comando.length() > 1 && comando != "nessuno" && comando != "null") {
          
          tft.fillRect(0, 0, 160, 16, ST77XX_BLUE);
          tft.setCursor(5, 4); tft.setTextColor(ST77XX_WHITE); 
          tft.print("Eseguo: " + comando);
          
          String rispostaFirebase = "";

          if (comando == "help") rispostaFirebase = "Comandi: reboot, force_send, clear_logs, uptime, scan_wifi, free_ram, get_raw";
          else if (comando == "reboot") {
            Firebase.RTDB.setString(&fbdoComandi, "meteo/comandi/risposta", "OK: Riavvio in corso...");
            delay(500); 
            Firebase.RTDB.setString(&fbdoComandi, "meteo/comandi/azione", "nessuno");
            inviaLog("[SISTEMA]", "Terminale: Riavvio forzato eseguito.");
            delay(1500);
            ESP.restart();
          }
          else if (comando == "uptime") {
            long sec = millis() / 1000;
            int h = sec / 3600; int m = (sec % 3600) / 60; int s = sec % 60;
            rispostaFirebase = "Uptime: " + String(h) + "h " + String(m) + "m " + String(s) + "s";
          }
          else if (comando == "free_ram") rispostaFirebase = "RAM Libera: " + String(ESP.getFreeHeap() / 1024) + " KB";
          else if (comando == "get_raw") rispostaFirebase = "T:" + String(temp_dht,1) + " V:" + String(velocita_kmh,1) + " Dir:" + direzione_vento + " P1h:" + String(pioggia_1h,1) + "mm P24h:" + String(pioggia_24h,1) + "mm";
          else if (comando == "scan_wifi") {
            int n = WiFi.scanNetworks();
            rispostaFirebase = "Reti trovate: " + String(n) + ". Segnale attuale: " + String(WiFi.RSSI()) + "dBm";
          }
          else if (comando == "force_send") {
            tempoFirebase = 0; 
            rispostaFirebase = "OK: Invio dati forzato eseguito.";
          }
          else if (comando == "clear_logs") {
            preferenze.clear(); 
            for(int i=0; i<maxLogLocali; i++) logLocali[i] = ""; 
            numeroLogSalvati = 0; indiceLog = 0; scrollLogOffset = 0;
            rispostaFirebase = "OK: Memoria log fisici svuotata.";
            inviaLog("[SISTEMA]", "Terminale: Log locali cancellati.");
          }
          else rispostaFirebase = "Comando sconosciuto. Digita 'help'.";

          Firebase.RTDB.setString(&fbdoComandi, "/meteo/comandi/risposta", rispostaFirebase);
          delay(200);
          Firebase.RTDB.setString(&fbdoComandi, "/meteo/comandi/azione", "nessuno");
          delay(1000); 

          if (statoAttuale == DASHBOARD) { disegnaImpalcaturaDashboard(); aggiornaValoriDashboard(); }
          else if (statoAttuale == MENU_LISTA) { disegnaMenu(); }
          else if (statoAttuale == PAGINA_LOG) { disegnaPaginaLog(); }
        }
      }
    }
  }

  // --- 5. INVIO A FIREBASE TEMPO REALE (Ogni 2 Secondi) ---
  if (tempoAttuale - tempoRealtime >= 2000) {
    tempoRealtime = tempoAttuale;
    bool sta_piovendo = (tempoAttuale - tempoUltimaGoccia < 5000);

    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      FirebaseJson rtJson;
      rtJson.set("vento_kmh", velocita_kmh); 
      rtJson.set("direzione_vento", direzione_vento); 
      rtJson.set("pioggia_1h", pioggia_1h);
      rtJson.set("pioggia_24h", pioggia_24h);
      rtJson.set("sta_piovendo", sta_piovendo); 
      
      rtJson.set("stato_dht", stato_dht_ok ? "OK" : "OFF");
      rtJson.set("stato_bmp", stato_bmp_ok ? "OK" : "OFF");
      rtJson.set("stato_anemoscopio", stato_anemoscopio_ok ? "OK" : "OFF");

      Firebase.RTDB.setJSON(&fbdo, "/meteo/realtime", &rtJson);
    }
  }

  // --- INVIO STORICO GRAFICO E AZZERAMENTO (Ogni 10 Min) ---
  if (tempoAttuale - tempoFirebase >= 600000) {
    tempoFirebase = tempoAttuale;
    
    if(stato_dht_ok) aggiungiPuntoGrafico(temp_dht, raffica_max);
    
    if (WiFi.status() == WL_CONNECTED && Firebase.ready() && stato_dht_ok && stato_bmp_ok) {
      FirebaseJson json;
      json.set("temperatura", temp_dht); 
      json.set("umidita", umidita);
      json.set("pressione", pressione); 
      json.set("altitudine", altitudine);
      json.set("vento_kmh", raffica_max);
      json.set("pioggia_10m", pioggia_10min);
      json.set("timestamp/.sv", "timestamp"); 
      Firebase.RTDB.pushJSON(&fbdo, "/meteo/storico", &json);
    }
    
    // Azzera i contatori volumetrici e di picco
    raffica_max = 0.0;
    pioggia_10min = 0.0;
  }
}
