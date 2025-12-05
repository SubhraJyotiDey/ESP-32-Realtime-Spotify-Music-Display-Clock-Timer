/*
 * PROJECT: The ISO-Deck (Spotify & Productivity Hub)
 * AUTHOR: [Your Name]
 * HARDWARE: ESP32, 16x2 I2C LCD, Rotary Encoder, RGB LED
 * FEATURES: Dual-Core Multitasking, Spotify API, Pomodoro, Alarm
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SpotifyArduino.h> // Library by witnessmenow
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Encoder.h>   // Library by Kevin Harrington
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>

// ================= USER CONFIGURATION =================
// 1. Wi-Fi Credentials
char ssid[] = "YOUR_WIFI_NAME";
char password[] = "YOUR_WIFI_PASS";

// 2. Spotify Keys (Get these from developer.spotify.com)
char clientId[] = "YOUR_CLIENT_ID";
char clientSecret[] = "YOUR_CLIENT_SECRET"; 
#define SPOTIFY_REFRESH_TOKEN "YOUR_REFRESH_TOKEN"

#define COUNTRY_CODE "IN"

// ================= PIN DEFINITIONS =================
#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_BUZZER 18
#define PIN_LED_R 16
#define PIN_LED_G 17
#define PIN_LED_B 5
#define PIN_ENC_CLK 25
#define PIN_ENC_DT 26
#define PIN_ENC_SW 27

// ================= BITMAPS (Inverted Headers) =================
// Custom graphics to create "Solid Block" text effect
const uint8_t CHAR_MAP[][8] = {
  {0x1F, 0x1F, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x00}, // 0: T
  {0x1F, 0x1F, 0x0E, 0x0E, 0x0E, 0x0E, 0x1F, 0x00}, // 1: I
  {0x1F, 0x1F, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x00}, // 2: M
  {0x1F, 0x1F, 0x00, 0x15, 0x15, 0x15, 0x00, 0x00}, // 3: E
  {0x1F, 0x1F, 0x00, 0x1D, 0x1D, 0x03, 0x00, 0x00}, // 4: S
  {0x1F, 0x1F, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, // 5: O
  {0x1F, 0x1F, 0x00, 0x19, 0x15, 0x13, 0x11, 0x00}, // 6: N
  {0x1F, 0x1F, 0x10, 0x17, 0x11, 0x11, 0x0E, 0x00}, // 7: G
  {0x1F, 0x1F, 0x11, 0x11, 0x0A, 0x0A, 0x0A, 0x00}, // 8: W
  {0x1F, 0x1F, 0x01, 0x11, 0x03, 0x05, 0x09, 0x00}, // 9: R
  {0x1F, 0x1F, 0x09, 0x05, 0x03, 0x05, 0x09, 0x00}, // 10: K
  {0x1F, 0x1F, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}, // 11: A
  {0x1F, 0x1F, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00}, // 12: L
};

// ================= OBJECTS =================
LiquidCrystal_I2C lcd(0x27, 16, 2);
ESP32Encoder encoder;
WiFiClientSecure client;
SpotifyArduino spotify(client, clientId, clientSecret, SPOTIFY_REFRESH_TOKEN);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800); // UTC+5:30 Offset

// ================= SHARED DATA (Dual Core Sync) =================
volatile int currentMode = 0; // 0=Auto, 1=Pomodoro, 2=Alarm

String sharedTime = "Loading..";
String sharedTrack = "";
String sharedArtist = "";
bool sharedIsPlaying = false;

// Flags
bool alarmEnabled = false;
bool pomoRunning = false;
int pomoMinutes = 25;
int pomoSeconds = 0;

// FreeRTOS Task Handles
TaskHandle_t TaskUI;
TaskHandle_t TaskNet;

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  
  // Pin Modes
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT); pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);

  // Encoder Init
  encoder.attachHalfQuad(PIN_ENC_DT, PIN_ENC_CLK);
  encoder.setCount(0);
  pinMode(PIN_ENC_CLK, INPUT_PULLUP); pinMode(PIN_ENC_DT, INPUT_PULLUP);

  // LCD Init
  lcd.init(); lcd.backlight();
  lcd.setCursor(0,0); lcd.print("ISO-DECK v1.0");
  lcd.setCursor(0,1); lcd.print("System Init...");
  
  // Launch Dual Core Tasks
  // TaskUI runs on Core 1 (Fast, UI/Inputs)
  xTaskCreatePinnedToCore(TaskUICode, "UI_Task", 10000, NULL, 1, &TaskUI, 1);
  // TaskNet runs on Core 0 (Slow, Network/Spotify)
  xTaskCreatePinnedToCore(TaskNetCode, "Net_Task", 10000, NULL, 0, &TaskNet, 0);
}

void loop() { 
  vTaskDelete(NULL); // Loop is unused in FreeRTOS
}

// ================= TASK 1: UI & HARDWARE (CORE 1) =================
// Handles Display, LED, Encoder, Button. High speed loop.
void TaskUICode(void * pvParameters) {
  long lastEnc = 0;
  String line0_prev = "", line1_prev = "";
  bool showSong = false;
  unsigned long lastSwap = 0;

  for(;;) {
    // 1. ENCODER INPUT
    long newEnc = encoder.getCount() / 2;
    if (newEnc != lastEnc) {
      if (newEnc > lastEnc) { currentMode++; if(currentMode > 2) currentMode=0; }
      else { currentMode--; if(currentMode < 0) currentMode=2; }
      lastEnc = newEnc;
      lcd.clear(); line0_prev=""; line1_prev="";
    }
    
    // 2. BUTTON INPUT
    if (digitalRead(PIN_ENC_SW) == LOW) {
      delay(50);
      if (digitalRead(PIN_ENC_SW) == LOW) {
        if(currentMode == 2) alarmEnabled = !alarmEnabled;
        if(currentMode == 1) pomoRunning = !pomoRunning;
        // Note: Spotify Play/Pause is handled via sharedIsPlaying flag logic if needed, 
        // but for simplicity in this version, we focus on local controls.
        while(digitalRead(PIN_ENC_SW) == LOW);
      }
    }

    // 3. HEADER MANAGEMENT (Inverted UI Logic)
    static int currentHeaderState = -1; 
    int targetHeader = 0; // 0=TIME, 1=SONG, 2=WORK, 3=ALRM
    
    if (currentMode == 1) targetHeader = 2; // WORK
    else if (currentMode == 2) targetHeader = 3; // ALRM
    else {
      // Auto Mode: Swap between Time and Song
      if (millis() - lastSwap > 4000) { lastSwap = millis(); showSong = !showSong; }
      if (sharedIsPlaying && showSong) targetHeader = 1; // SONG
      else targetHeader = 0; // TIME
    }

    if (targetHeader != currentHeaderState) {
      loadHeaderIcons(targetHeader);
      currentHeaderState = targetHeader;
      // Draw Header Block
      lcd.setCursor(0,0);
      for(int i=0; i<4; i++) lcd.write(i); 
    }

    // 4. DATA DISPLAY
    String line1 = "";
    
    if (currentMode == 0) { // AUTO
      if (currentHeaderState == 1) { // SONG
        line1 = sharedTrack.substring(0,16);
        // Mood Light: Seed random based on song title length for consistent colors
        long seed = sharedTrack.length(); randomSeed(seed);
        analogWrite(PIN_LED_R, random(0,150)); 
        analogWrite(PIN_LED_G, random(0,150)); 
        analogWrite(PIN_LED_B, random(0,150));
      } else { // TIME
        lcd.setCursor(5,0); lcd.print(sharedTime.substring(0,5)); // "12:00"
        line1 = "Status: ONLINE";
        // Default Light: Soft White/Blue
        analogWrite(PIN_LED_R, 0); analogWrite(PIN_LED_G, 20); analogWrite(PIN_LED_B, 30);
      }
    }
    else if (currentMode == 1) { // POMODORO
      // Header is "WORK"
      line1 = String(pomoMinutes) + ":" + String(pomoSeconds);
      if(pomoRunning) line1 += " >";
      analogWrite(PIN_LED_R, 50); analogWrite(PIN_LED_G, 0); analogWrite(PIN_LED_B, 0);
    }
    else if (currentMode == 2) { // ALARM
      // Header is "ALRM"
      line1 = alarmEnabled ? "Status: ON" : "Status: OFF";
      analogWrite(PIN_LED_R, 20); analogWrite(PIN_LED_G, 20); analogWrite(PIN_LED_B, 0);
    }

    printPadded(1, line1, line1_prev);
    line1_prev = line1;
    delay(10); // Keep UI responsive
  }
}

// ================= TASK 2: NET (CORE 0) =================
// Handles WiFi, Spotify API, NTP Time. Low speed loop.
void TaskNetCode(void * pvParameters) {
  WiFi.mode(WIFI_STA); WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  
  client.setInsecure(); timeClient.begin();
  
  for(;;) {
    timeClient.update();
    sharedTime = timeClient.getFormattedTime();
    
    // Check Spotify status every 3 seconds
    int status = spotify.getCurrentlyPlaying(saveData, COUNTRY_CODE);
    sharedIsPlaying = (status == 200);
    
    // Alarm Logic (7:00 AM)
    if(alarmEnabled && timeClient.getHours() == 7 && timeClient.getMinutes() == 0 && timeClient.getSeconds() == 0) {
       tone(PIN_BUZZER, 2000, 2000);
    }
    
    // Pomodoro Logic (Running here for accurate timing)
    static unsigned long lastPomoTick = 0;
    if (pomoRunning && millis() - lastPomoTick > 1000) {
       lastPomoTick = millis();
       if (pomoSeconds == 0) {
          if (pomoMinutes == 0) {
             pomoRunning = false;
             tone(PIN_BUZZER, 1000, 1000);
             pomoMinutes = 25; 
          } else { pomoMinutes--; pomoSeconds = 59; }
       } else { pomoSeconds--; }
    }
    
    delay(1000); // Wait 1 second
  }
}

// Callback to save Spotify data to shared variables
void saveData(CurrentlyPlaying cp) {
  sharedTrack = String(cp.trackName);
  sharedArtist = String(cp.artists[0].artistName);
}

// ================= HELPER FUNCTIONS =================

void loadHeaderIcons(int type) {
  // Swaps bitmaps into LCD memory based on mode
  // 0=TIME, 1=SONG, 2=WORK, 3=ALRM 
  uint8_t *c1, *c2, *c3, *c4;
  
  if (type == 0) { // TIME
    c1=(uint8_t*)CHAR_MAP[0]; c2=(uint8_t*)CHAR_MAP[1]; c3=(uint8_t*)CHAR_MAP[2]; c4=(uint8_t*)CHAR_MAP[3]; 
  }
  else if (type == 1) { // SONG
    c1=(uint8_t*)CHAR_MAP[4]; c2=(uint8_t*)CHAR_MAP[5]; c3=(uint8_t*)CHAR_MAP[6]; c4=(uint8_t*)CHAR_MAP[7];
  }
  else if (type == 2) { // WORK
    c1=(uint8_t*)CHAR_MAP[8]; c2=(uint8_t*)CHAR_MAP[5]; c3=(uint8_t*)CHAR_MAP[9]; c4=(uint8_t*)CHAR_MAP[10];
  }
  else { // ALARM (Using simulated mapping for A, L, R, M)
     c1=(uint8_t*)CHAR_MAP[11]; c2=(uint8_t*)CHAR_MAP[12]; c3=(uint8_t*)CHAR_MAP[9]; c4=(uint8_t*)CHAR_MAP[2];
  }
  lcd.createChar(0, c1); lcd.createChar(1, c2); lcd.createChar(2, c3); lcd.createChar(3, c4);
}

// Ensures text overwrites previous text completely
void printPadded(int row, String text, String prevText) {
  if (text.length() > 16) text = text.substring(0, 16);
  while (text.length() < 16) text += " ";
  if (text != prevText) {
    lcd.setCursor(0, row);
    lcd.print(text);
  }
}
