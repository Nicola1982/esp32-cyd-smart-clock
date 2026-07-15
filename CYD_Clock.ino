#include <WiFi.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite clockSprite = TFT_eSprite(&tft);

AsyncWebServer server(80);

const char* ntpServer = "pool.ntp.org";
const char* tzInfo     = "CET-1CEST,M3.5.0,M10.5.0/3"; 

// Colori di default
uint16_t BACK_COLOR = 0x0000;  
uint16_t TEXT_MAIN  = 0x07FF;  
uint16_t TEXT_SEC   = 0xF81F;  
uint16_t TEXT_MUTED = 0x7BEF;  

char html_main[8]  = "#00FFFF"; 
char html_sec[8]   = "#FF00FF";
char html_muted[8] = "#7BEF00";

int activeFont = 4;

// Stato della notifica mail (0 = nessuna mail, 1 = nuova mail)
volatile int newMailStatus = 0; 

// --- Nomi giorni e mesi in italiano (senza accenti per compatibilita' col font TFT) ---
const char* giorniIT[7] = {"Domenica", "Lunedi", "Martedi", "Mercoledi", "Giovedi", "Venerdi", "Sabato"};
const char* mesiIT[12]  = {"Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre"};

// --- Retroilluminazione automatica ---
#define BL_PIN 21          // Pin backlight CYD (verifica sul tuo modello se diverso)
#define BL_FREQ 5000
#define BL_RESOLUTION 8

const int BRIGHT_DAY   = 255; // luminosita' piena diurna
const int BRIGHT_NIGHT = 35;  // luminosita' minima notturna (~14%, ancora leggibile al buio)

// Finestre orarie di transizione (in minuti dalla mezzanotte)
const int DAY_START       = 7 * 60;        // 07:00 inizio alba
const int DAY_START_END   = 7 * 60 + 30;   // 07:30 luminosita' piena raggiunta
const int NIGHT_START     = 22 * 60;       // 22:00 inizio tramonto
const int NIGHT_START_END = 22 * 60 + 30;  // 22:30 buio raggiunto

int lastBrightness = -1; // per evitare scritture PWM ripetute inutili

// --- Meteo (Open-Meteo) ---
const float WEATHER_LAT = ;
const float WEATHER_LON = ;
const unsigned long WEATHER_INTERVAL = 15UL * 60UL * 1000UL; // aggiornamento ogni 15 minuti
unsigned long lastWeatherFetch = 0;

enum WeatherCategory { WX_SUNNY, WX_CLOUDY, WX_RAINY, WX_SNOWY, WX_STORMY, WX_FOGGY, WX_UNKNOWN };

float currentTemp = 0;
WeatherCategory currentWxCategory = WX_UNKNOWN;
bool weatherValid = false;

uint16_t colHEXto565(const char* hex) {
  long rgb = strtol(hex + 1, NULL, 16); 
  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void saveColorsConfig() {
  File configFile = LittleFS.open("/config.txt", "w");
  if (configFile) {
    configFile.println(html_main);
    configFile.println(html_sec);
    configFile.println(html_muted);
    configFile.println(activeFont);
    configFile.close();
  }
}

void loadColorsConfig() {
  if (LittleFS.exists("/config.txt")) {
    File configFile = LittleFS.open("/config.txt", "r");
    if (configFile) {
      String m = configFile.readStringUntil('\n'); m.trim();
      String s = configFile.readStringUntil('\n'); s.trim();
      String u = configFile.readStringUntil('\n'); u.trim();
      String f = configFile.readStringUntil('\n'); f.trim();
      
      if(m.length() == 7) strcpy(html_main, m.c_str());
      if(s.length() == 7) strcpy(html_sec, s.c_str());
      if(u.length() == 7) strcpy(html_muted, u.c_str());
      if(f.length() > 0)  activeFont = f.toInt();
      
      TEXT_MAIN = colHEXto565(html_main);
      TEXT_SEC  = colHEXto565(html_sec);
      TEXT_MUTED = colHEXto565(html_muted);
      configFile.close();
    }
  }
}

// Pagina HTML con pulsante di reset manuale della notifica
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CYD Clock Config</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background: #121214; color: #fff; padding: 20px; }
    h2 { color: #00ffff; margin-bottom: 25px; }
    .container { max-width: 400px; margin: auto; background: #1a1a24; padding: 25px; border-radius: 15px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
    .row { margin: 20px 0; display: flex; justify-content: space-between; align-items: center; font-size: 16px; }
    input[type="color"] { border: none; width: 65px; height: 40px; border-radius: 8px; cursor: pointer; background: none; }
    select { background: #2a2a35; color: white; border: 1px solid #ff00ff; padding: 8px; border-radius: 8px; width: 120px; font-size: 14px; }
    input[type="submit"], .btn-reset { color: white; border: none; padding: 12px 20px; font-size: 16px; border-radius: 8px; cursor: pointer; width: 100%; margin-top: 15px; font-weight: bold; }
    input[type="submit"] { background: #ff00ff; }
    input[type="submit"]:hover { background: #d000d0; }
    .btn-reset { background: #3a3a4a; text-decoration: none; display: block; box-sizing: border-box; }
    .btn-reset:hover { background: #4a4a5a; }
  </style>
</head>
<body>
  <div class="container">
    <h2>CYD Clock Setup</h2>
    <form action="/set" method="GET">
      <div class="row">
        <label>Stile del Font:</label>
        <select id="font" name="font">
          <option value="1">Pixel Retro (1)</option>
          <option value="2">Compact Clean (2)</option>
          <option value="4">Digital Matrix (4)</option>
        </select>
      </div>
      <div class="row">
        <label>Colore Orologio:</label>
        <input type="color" id="main" name="main">
      </div>
      <div class="row">
        <label>Colore Giorno:</label>
        <input type="color" id="sec" name="sec">
      </div>
      <div class="row">
        <label>Colore Data:</label>
        <input type="color" id="muted" name="muted">
      </div>
      <input type="submit" value="Applica e Salva">
    </form>
    <a href="/mail?status=0" class="btn-reset">Spegni Icona Mail</a>
  </div>

  <script>
    window.onload = function() {
      fetch('/getconfig').then(response => response.json()).then(data => {
        document.getElementById('main').value = data.main;
        document.getElementById('sec').value = data.sec;
        document.getElementById('muted').value = data.muted;
        document.getElementById('font').value = data.font;
      });
    }
  </script>
</body>
</html>
)rawliteral";

void drawStaticUI();
void updateClock();
void drawMailIcon(bool show);
void updateBacklight(struct tm &timeinfo);
WeatherCategory codeToCategory(int code);
void drawCloud(int x, int y, uint16_t color);
void drawWeatherIcon(WeatherCategory cat, int x, int y, uint16_t color);
void drawWeather();
void fetchWeather();

void setup() {
  Serial.begin(115200);

  if(!LittleFS.begin(true)){
    Serial.println("Errore LittleFS");
  }
  loadColorsConfig();

  // Inizializza il PWM per la retroilluminazione (API core ESP32 3.x)
  ledcAttach(BL_PIN, BL_FREQ, BL_RESOLUTION);
  ledcWrite(BL_PIN, BRIGHT_DAY); // parte a piena luminosita' finche' non sincronizza l'ora

  tft.init();
  tft.setRotation(1); 
  tft.invertDisplay(true); 
  tft.fillScreen(BACK_COLOR);

  tft.setTextColor(TEXT_MAIN, BACK_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Inizializzazione...", 160, 120, 4);

  WiFiManager wm;
  if (!wm.autoConnect("CYD-Clock")) {
    ESP.restart();
  }

  tft.fillScreen(BACK_COLOR);
  tft.drawString("Sincronizzo Ora...", 160, 100, 4);
  
  String ipStr = "IP: " + WiFi.localIP().toString();
  tft.drawString(ipStr, 160, 140, 2);

  configTzTime(tzInfo, ntpServer);

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });

  server.on("/getconfig", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{\"main\":\"" + String(html_main) + "\",\"sec\":\"" + String(html_sec) + "\",\"muted\":\"" + String(html_muted) + "\",\"font\":" + String(activeFont) + "}";
    request->send(200, "application/json", json);
  });

  // --- ENDPOINT PER RICEVERE LA NOTIFICA MAIL ---
  server.on("/mail", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("status")) {
      newMailStatus = request->getParam("status")->value().toInt();
      drawStaticUI();
    }
    request->redirect("/");
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("main") && request->hasParam("sec") && request->hasParam("muted") && request->hasParam("font")) {
      strcpy(html_main, request->getParam("main")->value().c_str());
      strcpy(html_sec, request->getParam("sec")->value().c_str());
      strcpy(html_muted, request->getParam("muted")->value().c_str());
      activeFont = request->getParam("font")->value().toInt();
      
      saveColorsConfig();

      TEXT_MAIN = colHEXto565(html_main);
      TEXT_SEC  = colHEXto565(html_sec);
      TEXT_MUTED = colHEXto565(html_muted);
      
      drawStaticUI(); 
    }
    request->redirect("/");
  });

  server.begin(); 

  clockSprite.createSprite(320, 90);
  drawStaticUI();

  // Imposta subito la luminosita' corretta in base all'ora appena sincronizzata
  updateBacklight(timeinfo);

  // Primo fetch meteo all'avvio
  fetchWeather();
  lastWeatherFetch = millis();
}

void loop() {
  updateClock();

  // Aggiornamento meteo periodico, non bloccante rispetto al ciclo dell'orologio
  if (millis() - lastWeatherFetch >= WEATHER_INTERVAL) {
    fetchWeather();
    lastWeatherFetch = millis();
  }

  delay(1000); 
}

void drawStaticUI() {
  tft.fillScreen(BACK_COLOR);
  
  tft.drawRect(5, 5, 310, 230, TEXT_MUTED);
  tft.drawRect(7, 7, 306, 226, BACK_COLOR); 
  
  tft.fillRect(5, 5, 15, 3, TEXT_SEC);
  tft.fillRect(5, 5, 3, 15, TEXT_SEC);
  tft.fillRect(300, 5, 15, 3, TEXT_SEC);
  tft.fillRect(312, 5, 3, 15, TEXT_SEC);

  drawMailIcon(newMailStatus == 1);

  // Ridisegna l'icona meteo (se abbiamo gia' dei dati validi)
  drawWeather();
}

void drawMailIcon(bool show) {
  int x = 148;
  int y = 14;
  int w = 24;
  int h = 14;

  if (show) {
    tft.drawRect(x, y, w, h, TEXT_SEC);
    tft.drawLine(x, y, x + (w/2), y + (h/2), TEXT_SEC);
    tft.drawLine(x + w, y, x + (w/2), y + (h/2), TEXT_SEC);
  } else {
    tft.fillRect(x - 1, y - 1, w + 2, h + 2, BACK_COLOR);
  }
}

// Calcola e applica la luminosita' del backlight in base all'orario corrente,
// con una transizione morbida tra notte e giorno invece di uno scatto secco.
void updateBacklight(struct tm &timeinfo) {
  int totalMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int brightness;

  if (totalMinutes >= DAY_START_END && totalMinutes < NIGHT_START) {
    brightness = BRIGHT_DAY;
  } else if (totalMinutes < DAY_START || totalMinutes >= NIGHT_START_END) {
    brightness = BRIGHT_NIGHT;
  } else if (totalMinutes >= DAY_START && totalMinutes < DAY_START_END) {
    float ratio = (float)(totalMinutes - DAY_START) / (DAY_START_END - DAY_START);
    brightness = BRIGHT_NIGHT + (int)((BRIGHT_DAY - BRIGHT_NIGHT) * ratio);
  } else {
    float ratio = (float)(totalMinutes - NIGHT_START) / (NIGHT_START_END - NIGHT_START);
    brightness = BRIGHT_DAY - (int)((BRIGHT_DAY - BRIGHT_NIGHT) * ratio);
  }

  if (brightness != lastBrightness) {
    ledcWrite(BL_PIN, brightness);
    lastBrightness = brightness;
  }
}

// --- METEO ---

// Traduce i codici meteo WMO (usati da Open-Meteo) in una categoria semplificata
WeatherCategory codeToCategory(int code) {
  if (code == 0 || code == 1) return WX_SUNNY;
  if (code == 2 || code == 3) return WX_CLOUDY;
  if (code == 45 || code == 48) return WX_FOGGY;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WX_RAINY;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WX_SNOWY;
  if (code == 95 || code == 96 || code == 99) return WX_STORMY;
  return WX_UNKNOWN;
}

// Disegna una nuvola stilizzata (base per cloudy/rainy/snowy/stormy) in un box 24x28
void drawCloud(int x, int y, uint16_t color) {
  tft.fillCircle(x + 6, y + 8, 6, color);
  tft.fillCircle(x + 13, y + 5, 7, color);
  tft.fillCircle(x + 20, y + 8, 6, color);
  tft.fillRoundRect(x + 2, y + 8, 22, 8, 3, color);
}

// Disegna l'icona meteo in base alla categoria, in un box ~24x28 con angolo in alto a sinistra (x,y)
void drawWeatherIcon(WeatherCategory cat, int x, int y, uint16_t color) {
  switch (cat) {
    case WX_SUNNY:
      tft.fillCircle(x + 12, y + 10, 6, color);
      tft.drawLine(x + 12, y - 2, x + 12, y + 2, color);
      tft.drawLine(x + 12, y + 18, x + 12, y + 22, color);
      tft.drawLine(x, y + 10, x + 4, y + 10, color);
      tft.drawLine(x + 20, y + 10, x + 24, y + 10, color);
      tft.drawLine(x + 4, y + 2, x + 7, y + 5, color);
      tft.drawLine(x + 17, y + 15, x + 20, y + 18, color);
      tft.drawLine(x + 20, y + 2, x + 17, y + 5, color);
      tft.drawLine(x + 7, y + 15, x + 4, y + 18, color);
      break;

    case WX_CLOUDY:
      drawCloud(x, y, color);
      break;

    case WX_RAINY:
      drawCloud(x, y, color);
      tft.drawLine(x + 7, y + 18, x + 5, y + 23, color);
      tft.drawLine(x + 13, y + 18, x + 11, y + 23, color);
      tft.drawLine(x + 19, y + 18, x + 17, y + 23, color);
      break;

    case WX_SNOWY:
      drawCloud(x, y, color);
      tft.drawLine(x + 6, y + 19, x + 8, y + 21, color);
      tft.drawLine(x + 8, y + 19, x + 6, y + 21, color);
      tft.drawLine(x + 18, y + 19, x + 20, y + 21, color);
      tft.drawLine(x + 20, y + 19, x + 18, y + 21, color);
      break;

    case WX_STORMY:
      drawCloud(x, y, color);
      tft.drawLine(x + 13, y + 17, x + 9, y + 23, color);
      tft.drawLine(x + 9, y + 23, x + 15, y + 23, color);
      tft.drawLine(x + 15, y + 23, x + 11, y + 29, color);
      break;

    case WX_FOGGY:
      for (int i = 0; i < 4; i++) {
        tft.drawLine(x, y + 4 + i * 5, x + 24, y + 4 + i * 5, color);
      }
      break;

    default:
      tft.setTextDatum(MC_DATUM);
      tft.drawString("?", x + 12, y + 10, 2);
      break;
  }
}

// Pulisce e ridisegna l'area meteo (angolo in alto a destra) con icona + temperatura
void drawWeather() {
  tft.fillRect(180, 4, 115, 30, BACK_COLOR);

  if (!weatherValid) return;

  drawWeatherIcon(currentWxCategory, 255, 6, TEXT_SEC);

  char tempBuff[8];
  snprintf(tempBuff, sizeof(tempBuff), "%.0fC", currentTemp);

  tft.setTextColor(TEXT_MUTED, BACK_COLOR);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(tempBuff, 248, 16, 2);
}

// Interroga Open-Meteo e aggiorna temperatura + icona per San Pietro in Cariano (VR)
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Endpoint pubblico senza dati sensibili: saltiamo la verifica del certificato CA

  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LAT, 4) +
               "&longitude=" + String(WEATHER_LON, 4) +
               "&current=temperature_2m,weather_code&timezone=Europe%2FRome";

  if (!http.begin(client, url)) {
    Serial.println("Errore inizializzazione richiesta meteo");
    return;
  }

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();

    StaticJsonDocument<512> doc; // ArduinoJson v6 - se usi v7 sostituisci con JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      currentTemp = doc["current"]["temperature_2m"].as<float>();
      int code = doc["current"]["weather_code"].as<int>();
      currentWxCategory = codeToCategory(code);
      weatherValid = true;
      drawWeather();
    } else {
      Serial.print("Errore parsing JSON meteo: ");
      Serial.println(err.c_str());
    }
  } else {
    Serial.printf("Richiesta meteo fallita, codice HTTP: %d\n", httpCode);
  }

  http.end();
}

void updateClock() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char fullTimeBuff[12];
  char dateBuff[32];

  strftime(fullTimeBuff, sizeof(fullTimeBuff), "%H:%M:%S", &timeinfo);

  const char* dayNameBuff = giorniIT[timeinfo.tm_wday];
  snprintf(dateBuff, sizeof(dateBuff), "%d %s %d", timeinfo.tm_mday, mesiIT[timeinfo.tm_mon], timeinfo.tm_year + 1900);

  clockSprite.fillSprite(BACK_COLOR);
  
  int clockSize = (activeFont == 4) ? 2 : 4; 
  
  clockSprite.setTextSize(clockSize); 
  clockSprite.setTextColor(TEXT_MAIN, BACK_COLOR); 
  clockSprite.setTextDatum(MC_DATUM); 
  clockSprite.drawString(fullTimeBuff, 160, 45, activeFont); 
  clockSprite.pushSprite(0, 40);

  tft.fillRect(15, 145, 290, 75, BACK_COLOR);
  
  tft.setTextColor(TEXT_SEC, BACK_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(dayNameBuff, 160, 150, activeFont); 
  
  tft.setTextColor(TEXT_MUTED, BACK_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(dateBuff, 160, 185, activeFont); 

  updateBacklight(timeinfo);
}
