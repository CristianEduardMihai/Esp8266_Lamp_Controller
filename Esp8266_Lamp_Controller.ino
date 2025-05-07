/*
  🌟 ESP8266 Lamp Controller 🌟
  A customizable Wi-Fi enabled dimmable lamp with web interface

  Features:
  - Web UI for brightness control and auto-off timer
  - Wi-Fi provisioning via captive portal (AP mode)
  - Button toggle input with debounce
  - PWM dimming using GPIO2 (PNP high-side)
  - EEPROM storage for Wi-Fi credentials

  Hardware:
  - LED via PNP transistor on GPIO2
  - Push-button toggle on GPIO1 (TX)

  Libraries required:
    ESP8266WiFi
    ESP8266WebServer
    DNSServer
    EEPROM
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

// Access Point credentials
const char* apSSID = "ESP8266 Lamp";
const char* apPass = "password123";

// GPIO Pins
const int LED_PIN = 2;
const int BUTTON_PIN = 1; // TX

// Button debounce
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50;
int lastStableButtonState = HIGH;

// Globals
ESP8266WebServer server(80);
DNSServer dnsServer;

int brightnessPercent = 0;
unsigned long lastOnTime = 0;
int timerMinutes = 0;
bool lampOn = false;
bool lastButtonState = LOW;

// --- EEPROM helpers ---
String readString(int addr, int maxLen) {
  int len = EEPROM.read(addr);
  if (len <= 0 || len > maxLen) return String();
  String out;
  for (int i = 0; i < len; i++) out += char(EEPROM.read(addr + 1 + i));
  return out;
}

void writeString(int addr, const String &str, int maxLen) {
  int len = min((int)str.length(), maxLen);
  EEPROM.write(addr, len);
  for (int i = 0; i < len; i++) EEPROM.write(addr + 1 + i, str[i]);
  EEPROM.commit();
}

// --- Lamp control ---
void applyBrightness() {
  uint8_t effectivePct = (brightnessPercent == 0) ? 0 : map(brightnessPercent, 1, 100, 75, 100);
  uint16_t pwm = map(effectivePct, 0, 100, 1023, 0); // Invert for PNP
  analogWrite(LED_PIN, pwm);
  lampOn = (effectivePct > 0);
  if (lampOn) lastOnTime = millis();
}

// --- Web UI ---
String css =
  "<style>"
  "body{background:#ffe0f0;font-family:Arial,sans-serif;color:#333;text-align:center;padding:20px;}"
  "h1{color:#d6336c;}"
  "input[type=range]{width:80%;margin:20px 0;}"
  "button{display:block;margin:0 auto;padding:10px 20px;background:#d6336c;color:#fff;border:none;border-radius:5px;cursor:pointer;}"
  "button:hover{background:#c1235b;}"
  "hr{border:1px solid #f2c1d1;margin:20px 0;}"
  "</style>";

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Lamp Control</title>" + css + "</head><body>";
  html += "<h1>🌟 Lamp Controller 🌟</h1>";
  html += "<form action='/setBrightness' method='POST'>";
  html += "<input type='range' name='brightness' min='0' max='100' value='" + String(brightnessPercent) + "' oninput='this.nextElementSibling.value=this.value'>";
  html += "<output>" + String(brightnessPercent) + "</output>%";
  html += "<button>Apply</button></form>";
  html += "<hr><h2>Auto-off Timer</h2>";
  html += "<form action='/setTimer' method='POST'>";
  html += "<input type='number' name='timer' min='0' value='" + String(timerMinutes) + "'> min";
  html += "<button>Set Timer</button></form><p>0 = disabled</p>";
  html += "<hr><a href='/wifi'>Configure Wi-Fi</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSetBrightness() {
  if (server.hasArg("brightness")) {
    brightnessPercent = server.arg("brightness").toInt();
    applyBrightness();
  }
  server.sendHeader("Location","/"); server.send(302,"text/plain","{}");
}

void handleSetTimer() {
  if (server.hasArg("timer")) timerMinutes = server.arg("timer").toInt();
  server.sendHeader("Location","/"); server.send(302,"text/plain","{}");
}

void handleWifiConfig() {
  String ssid = readString(1,32), pass = readString(33,64);
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Wi-Fi Config</title>"+css+"</head><body>";
  html += "<h1>Configure Wi-Fi</h1><form action='/saveWifi' method='POST'>";
  html += "SSID:<input type='text' name='ssid' value='"+ssid+"'><br>";
  html += "Password:<input type='password' name='pass' value='"+pass+"'><br><br>";
  html += "<button>Save & Restart</button></form></body></html>";
  server.send(200,"text/html",html);
}

void handleSaveWifi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    writeString(1,server.arg("ssid"),32);
    writeString(33,server.arg("pass"),64);
  }
  server.send(200,"text/html","<p>Saved! Restarting...</p>"); delay(2000); ESP.restart();
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID,apPass);
  dnsServer.start(53,"*",WiFi.softAPIP());
}

void setup() {
  Serial.begin(9600); delay(100);
  pinMode(LED_PIN,OUTPUT); analogWrite(LED_PIN,0);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  EEPROM.begin(512);

  String ssid = readString(1,32), pass = readString(33,64);
  WiFi.hostname("ESP8266-Lamp");
  if (ssid.length()) {
    WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(),pass.c_str());
    unsigned long start=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-start<10000) delay(500);
    if (WiFi.status()!=WL_CONNECTED) startAPMode();
  } else startAPMode();

  server.on("/",HTTP_GET,handleRoot);
  server.on("/setBrightness",HTTP_POST,handleSetBrightness);
  server.on("/setTimer",HTTP_POST,handleSetTimer);
  server.on("/wifi",HTTP_GET,handleWifiConfig);
  server.on("/saveWifi",HTTP_POST,handleSaveWifi);
  server.onNotFound([](){ server.sendHeader("Location","/"); server.send(302,"text/plain","{}"); });
  server.begin();

  brightnessPercent = 0;
  applyBrightness();
}

void loop() {
  if (WiFi.getMode() & WIFI_AP) dnsServer.processNextRequest();
  server.handleClient();

  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState != lastButtonState) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentButtonState != lastStableButtonState) {
      lastStableButtonState = currentButtonState;
      if (currentButtonState == LOW) {
        brightnessPercent = lampOn ? 0 : 100;
        applyBrightness();
        Serial.println("Button pressed - toggled");
      }
    }
  }
  lastButtonState = currentButtonState;

  if (lampOn && timerMinutes > 0 && millis() - lastOnTime >= (unsigned long)timerMinutes * 60000UL) {
    brightnessPercent = 0;
    applyBrightness();
    Serial.println("Auto-off triggered");
  }
}
