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
#include <ESP8266HTTPUpdateServer.h>

// AP credentials
const char* apSSID = "ESP8266 Lamp";
const char* apPass = "password123";

// Pins
const int LED_PIN = 2;     // GPIO2 (PWM)
const int BUTTON_PIN = 3;  // RX toggle button

unsigned long lastDebounceTime = 0;
const int debounceDelay = 50;
int lastStableButtonState = HIGH;        // Assume button starts unpressed

// Globals
ESP8266WebServer server(80);
DNSServer dnsServer;
ESP8266HTTPUpdateServer httpUpdater;

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
  // Scale 1-100% → 75-100%, 0% → 0 (off)
  uint8_t effectivePct = (brightnessPercent == 0) ? 0 : map(brightnessPercent, 1, 100, 75, 100);
  
  // Invert for PNP (0% → full PWM, 100% → zero PWM)
  uint16_t pwm = map(effectivePct, 0, 100, 1023, 0);
  
  analogWrite(LED_PIN, pwm);
  lampOn = (effectivePct > 0); // Now 0% sets lampOn to false
  if (lampOn) lastOnTime = millis();
}

// --- Web UI ---
String css =
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
  "<style>"
  "body{background:#ffe0f0;font-family:Arial,sans-serif;color:#333;text-align:center;padding:20px;}"
  "h1{color:#d6336c;font-size:clamp(1.5rem, 6vw, 2rem);}" 
  "input[type=range]{width:80%;max-width:500px;height:40px;margin:20px 0;}"
  "button{display:block;margin:0 auto;padding:14px 28px;background:#d6336c;color:#fff;border:none;border-radius:5px;cursor:pointer;font-size:1.1rem;min-width:200px;}"
  "button:hover{background:#c1235b;}"
  "hr{border:1px solid #f2c1d1;margin:20px 0;}"
  
  /* Mobile-specific adjustments */
  "input[type=number],input[type=text],input[type=password],input[type=file]{"
  "  padding:12px;font-size:1rem;width:60%;max-width:300px;"
  "}"
  "output{font-size:1.4rem;font-weight:bold;min-width:50px;display:inline-block;}"
  
  /* Media query for small screens */
  "@media (max-width:600px){"
  "  body{padding:15px;}"
  "  input[type=range]{width:100%;}"
  "  button{width:100%;max-width:300px;padding:16px;}"
  "}"
  "</style>";

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Lamp Control</title>" + css + 
  "<script>"
  "function updateBrightness(val) {"
  "  document.querySelector('output').innerHTML = val;"
  "  var xhr = new XMLHttpRequest();"
  "  xhr.open('POST', '/setBrightness?brightness=' + val, true);"
  "  xhr.send();"
  "}"
  "</script>"
  "</head><body>";
  
  html += "<h1>🌟 Lamp Controller 🌟</h1>";
  html += "<form onsubmit='return false;'>";
  html += "<input type='range' name='brightness' min='0' max='100' value='" + String(brightnessPercent) + 
          "' onchange='updateBrightness(this.value)'>";
  html += "<br><output>" + String(brightnessPercent) + "</output>%";
  html += "</form>";
  
  // Rest of your HTML remains the same
  html += "<hr><h2>Auto-off Timer</h2>";
  html += "<form action='/setTimer' method='POST'>";
  html += "<input type='number' name='timer' min='0' value='" + String(timerMinutes) + "' style='font-size:1.1rem;padding:8px;'> min";
  html += "<button>Set Timer</button></form><p>0 = disabled</p>";
  html += "<hr><a href='/setup'>Device Setup</a>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleSetBrightness() {
  if (server.hasArg("brightness")) {
    brightnessPercent = server.arg("brightness").toInt();
    applyBrightness();
  }
  server.send(200, "text/plain", "");
}

void handleSetTimer() {
  if (server.hasArg("timer")) timerMinutes = server.arg("timer").toInt();
  server.sendHeader("Location","/"); server.send(302,"text/plain","{}");
}

void handleSetup() {
  String ssid = readString(1,32), pass = readString(33,64);
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Device Setup</title>" + css + "</head><body>";
  html += "<h1>✨ Device Setup ✨</h1>";
  
  // Wi-Fi Configuration Section
  html += "<h2>Wi-Fi Configuration</h2>";
  html += "<form action='/saveWifi' method='POST'>";
  html += "SSID: <input type='text' name='ssid' value='" + ssid + "' style='font-size:1.1rem;padding:8px;'><br><br>";
  html += "Password: <input type='password' name='pass' value='" + pass + "' style='font-size:1.1rem;padding:8px;'><br><br>";
  html += "<button>Save Wi-Fi & Restart</button></form>";
  
  // OTA Update Section
  html += "<hr><h2>Firmware Update</h2>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='update' style='font-size:1.1rem;padding:8px;'><br><br>";
  html += "<button>Upload Firmware</button>";
  html += "</form>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
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
  pinMode(BUTTON_PIN,INPUT);
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
  server.on("/setBrightness", HTTP_GET, handleSetBrightness);
  server.on("/setTimer",HTTP_POST,handleSetTimer);
  server.on("/setup", HTTP_GET, handleSetup);
  server.on("/saveWifi",HTTP_POST,handleSaveWifi);
  server.onNotFound([](){ server.sendHeader("Location","/"); server.send(302,"text/plain","{}"); });
  server.begin();
  httpUpdater.setup(&server);  // Enable OTA update handler

  brightnessPercent = 0;
  applyBrightness();
}

void loop() {
  if (WiFi.getMode() & WIFI_AP) dnsServer.processNextRequest();
  server.handleClient();

  // Read current button state
  int currentButtonState = digitalRead(BUTTON_PIN);

  // Check if button state changed (noise or press)
  if (currentButtonState != lastButtonState) {
    lastDebounceTime = millis();  // Reset debounce timer
  }

  // Only proceed if the state has been stable for debounceDelay
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Check if current state differs from last stable state
    if (currentButtonState != lastStableButtonState) {
      lastStableButtonState = currentButtonState;

      // Only trigger on falling edge (HIGH→LOW)
      if (currentButtonState == LOW) {
        // Toggle lamp state
        brightnessPercent = lampOn ? 0 : 100;
        applyBrightness();
        Serial.println("Button pressed - toggled");
      }
    }
  }

  lastButtonState = currentButtonState;  // For edge detection

  // Auto-off timer check (keep existing code)
  if (lampOn && timerMinutes > 0 && millis() - lastOnTime >= (unsigned long)timerMinutes * 60000UL) {
    brightnessPercent = 0;
    applyBrightness();
    Serial.println("Auto-off triggered");
  }
}