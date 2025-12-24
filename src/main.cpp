#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <FS.h>

// ESP8266Audio library for proper WAV playback
#include "AudioFileSourceSD.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

// ===== BOARD-SPECIFIC CONFIGURATION =====
#if defined(BOARD_CYD_RESISTIVE)
  // ESP32-2432S028R (E32R28T) with XPT2046 Resistive Touch
  // Touch uses SEPARATE SPI bus from display!
  // Touch SPI: SCLK=25, MOSI=32, MISO=39, CS=33, IRQ=36
  #include <XPT2046_Touchscreen.h>
  
  #define TOUCH_CS   33
  #define TOUCH_IRQ  36
  #define TOUCH_SCLK 25
  #define TOUCH_MOSI 32
  #define TOUCH_MISO 39
  #define TFT_BACKLIGHT 21
  #define BOARD_NAME "ESP32-2432S028R (Resistive)"
  
  // SD Card pins (dedicated SPI)
  #define SD_CS    5
  #define SD_MOSI  23
  #define SD_MISO  19
  #define SD_SCLK  18
  
  // Touch calibration values (adjust for your specific board)
  #define TOUCH_MIN_X 300
  #define TOUCH_MAX_X 3900
  #define TOUCH_MIN_Y 300
  #define TOUCH_MAX_Y 3900
  
  // Create second SPI bus for touch
  SPIClass touchSPI(HSPI);
  XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

#elif defined(BOARD_CYD_CAPACITIVE)
  // JC2432W328C (Guition) with CST816S Capacitive Touch (I2C)
  // Official pin config from: https://github.com/rzeldent/platformio-espressif32-sunton
  #include <Wire.h>
  #define TOUCH_SDA 33
  #define TOUCH_SCL 32
  #define TOUCH_INT 21   // Note: NOT 36!
  #define TOUCH_RST 25
  #define TFT_BACKLIGHT 27
  #define CST816S_ADDR 0x15
  #define BOARD_NAME "JC2432W328C (Capacitive)"
  
  // SD Card pins - TODO: Verify for this board
  #define SD_CS    5
  #define SD_MOSI  23
  #define SD_MISO  19
  #define SD_SCLK  18

#else
  #error "No board defined! Use -DBOARD_CYD_RESISTIVE or -DBOARD_CYD_CAPACITIVE"
#endif

// ===== DISPLAY CONFIGURATION =====
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// ===== COLOR DEFINITIONS =====
#define COLOR_RED       0xF800
#define COLOR_YELLOW    0xFFE0
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_WHITE     0xFFFF
#define COLOR_BLACK     0x0000
#define COLOR_GRAY      0x8410
#define COLOR_DARKGRAY  0x4208
#define COLOR_ORANGE    0xFD20

// ===== UI LAYOUT CONSTANTS =====
#define HEADER_HEIGHT    36
#define BUTTON_HEIGHT    40
#define BUTTON_MARGIN    4
#define BUTTON_X         10
#define BUTTON_WIDTH     (SCREEN_WIDTH - 20)
#define LIST_TOP         (HEADER_HEIGHT + 4)
#define LIST_HEIGHT      (SCREEN_HEIGHT - LIST_TOP - 36)  // Leave room for scroll indicators
#define VISIBLE_BUTTONS  ((LIST_HEIGHT) / (BUTTON_HEIGHT + BUTTON_MARGIN))

// Volume control positions
#define VOL_MINUS_X      200
#define VOL_PLUS_X       250
#define VOL_BTN_Y        4
#define VOL_BTN_SIZE     28
#define VOL_NUM_X        290
#define VOL_NUM_Y        18

// Scroll indicator positions
#define SCROLL_Y         (SCREEN_HEIGHT - 28)
#define SCROLL_UP_X      120
#define SCROLL_DOWN_X    180
#define SCROLL_BTN_W     40
#define SCROLL_BTN_H     24

// ===== SOUND DATA =====
#define MAX_SOUNDS 20

struct SoundEntry {
  char filename[16];   // e.g., "0001.wav"
  char title[32];      // e.g., "Achievement Bell"
};

SoundEntry sounds[MAX_SOUNDS];
int soundCount = 0;
int scrollOffset = 0;
int volume = 5;  // 0-10
const int MAX_VOLUME = 10;
bool sdCardOk = false;

// ===== GLOBAL OBJECTS =====
TFT_eSPI tft = TFT_eSPI();
SPIClass sdSPI(VSPI);  // Use VSPI for SD card

// Touch debounce
const unsigned long TOUCH_DEBOUNCE_MS = 200;
unsigned long lastTouchMillis = 0;

// ===== AUDIO CONFIGURATION =====
#define SPEAKER_DAC_PIN 26   // DAC output pin for audio (used for synthesized tones)
#define BEEP_FREQ 1000       // Beep frequency in Hz
#define BEEP_DURATION 200    // Beep duration in ms

// ESP8266Audio objects for WAV playback
AudioGeneratorWAV *wav = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;
bool audioPlaying = false;
int currentlyPlayingIndex = -1;  // Track which sound is playing for UI update
uint32_t wavStopPosition = 0;    // Position in file to stop playback (for early cutoff)

// Hardcoded sound indices
#define SOUND_BEEP   0
#define SOUND_SIREN  1
#define SOUND_CHIME  2
#define SOUND_LASER  3
#define NUM_BUILTIN_SOUNDS 4

#if defined(BOARD_CYD_RESISTIVE)
  #define SPEAKER_EN_PIN 4   // Speaker amplifier enable pin (resistive board only)
#endif

// ===== FUNCTION DECLARATIONS =====
bool readTouch(int &screenX, int &screenY);
void handleTouch(int touchX, int touchY);
void drawUI();
void drawHeader();
void drawVolumeControls();
void drawSoundButtons();
void drawScrollIndicators();
void drawButton(int x, int y, int w, int h, const char* label, uint16_t bgColor, uint16_t textColor);
void playSound(int index);
void resetPlayingButton();
void playBeep();
void playSiren();
void playChime();
void playLaser();
void playTone(int freqHz, int durationMs, int vol);
bool playWavFile(const char* filename);
void reinitTouch();
int getTouchedButton(int touchX, int touchY);
bool initSDCard();
bool parseIndexCSV();
void addBeepSound();

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("CYD Sound Board - Starting...");
  Serial.println(BOARD_NAME);
  Serial.println("========================================");

  // Initialize backlight pin and turn it on
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  Serial.println("Backlight ON");

  // Initialize speaker amplifier (resistive board has enable pin)
#if defined(BOARD_CYD_RESISTIVE)
  pinMode(SPEAKER_EN_PIN, OUTPUT);
  digitalWrite(SPEAKER_EN_PIN, LOW);  // Enable speaker amplifier (active LOW)
  Serial.println("Speaker amplifier enabled (GPIO 4 = LOW)");
#elif defined(BOARD_CYD_CAPACITIVE)
  // GPIO 4 is RGB LED Red on capacitive board - set HIGH to keep LED off
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  Serial.println("GPIO 4 set HIGH (LED off)");
#endif

  // Initialize display
  tft.init();
  tft.setRotation(1);  // Landscape mode
  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  Serial.println("Display initialized");

  tft.drawString("Initializing touch...", 160, 120);

  // ===== TOUCH CONTROLLER INITIALIZATION =====
#if defined(BOARD_CYD_RESISTIVE)
  // XPT2046 Resistive Touch - uses SEPARATE SPI bus from display
  Serial.printf("Touch pins: CS=%d, IRQ=%d, SCLK=%d, MOSI=%d, MISO=%d\n", 
                TOUCH_CS, TOUCH_IRQ, TOUCH_SCLK, TOUCH_MOSI, TOUCH_MISO);
  
  // Initialize the separate SPI bus for touch (HSPI)
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);  // Match display rotation
  Serial.println("XPT2046 touch controller initialized on HSPI");

#elif defined(BOARD_CYD_CAPACITIVE)
  // CST816S Capacitive Touch (I2C)
  Serial.printf("Touch pins: SDA=%d, SCL=%d, RST=%d, INT=%d\n", 
                TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
  
  // Configure RST pin and perform reset
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(100);  // Wait for CST816S to boot
  Serial.println("Touch controller reset complete");

  // Configure INT pin
  pinMode(TOUCH_INT, INPUT);

  // Initialize I2C for touch on correct pins (SDA=33, SCL=32)
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  delay(50);
  
  // Verify touch controller is present and read info
  Wire.beginTransmission(CST816S_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.println("CST816S found at 0x15");
    
    // Read chip info
    Wire.beginTransmission(CST816S_ADDR);
    Wire.write(0xA7);  // Chip ID register
    Wire.endTransmission(false);
    Wire.requestFrom(CST816S_ADDR, 3);
    if (Wire.available() >= 3) {
      byte chipId = Wire.read();
      byte projId = Wire.read();
      byte fwVer = Wire.read();
      Serial.printf("  Chip ID: 0x%02X, Project: %d, FW: %d\n", chipId, projId, fwVer);
    }
  } else {
    Serial.println("WARNING: CST816S not found!");
  }
#endif

  Serial.println("Touch controller ready");

  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Initializing SD card...", SCREEN_WIDTH/2, SCREEN_HEIGHT/2);

  // Add the beep sound first (always available, no SD needed)
  addBeepSound();

  // Initialize SD card and load sound list
  if (initSDCard()) {
    parseIndexCSV();
  }

  if (soundCount == NUM_BUILTIN_SOUNDS) {  // Only built-in sounds available
    Serial.println("Only built-in sounds available (no SD sounds loaded)");
  }

  // Initialize audio output using ESP32 internal DAC
  // Internal DAC uses GPIO25 (left/channel 1) and GPIO26 (right/channel 2)
  // NOTE: Resistive board uses GPIO25 for touch SPI clock, so we must use mono
  // and output only to the right channel (GPIO26) to avoid conflict
  out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);
  out->SetOutputModeMono(true);  // Use mono mode to avoid GPIO25 conflict
  out->SetGain(0.5);  // Start at 50% gain
  Serial.println("Audio I2S output initialized (internal DAC, mono on GPIO26)");

  // Draw the main UI
  drawUI();

  Serial.println("Ready! Touch screen to interact.");
}

// ===== TOUCH READ FUNCTION =====
bool readTouch(int &screenX, int &screenY) {
#if defined(BOARD_CYD_RESISTIVE)
  // XPT2046 Resistive Touch
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    
    // Map raw values to screen coordinates
    screenX = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
    screenY = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);
    
    // Clamp to screen bounds
    screenX = constrain(screenX, 0, SCREEN_WIDTH - 1);
    screenY = constrain(screenY, 0, SCREEN_HEIGHT - 1);
    
    return true;
  }
  return false;

#elif defined(BOARD_CYD_CAPACITIVE)
  // CST816S Capacitive Touch - Direct I2C register reads
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write(0x02);  // Start at finger count register
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  
  Wire.requestFrom(CST816S_ADDR, 5);
  if (Wire.available() >= 5) {
    uint8_t fingers = Wire.read();  // 0x02 - finger count
    uint8_t xh = Wire.read();       // 0x03
    uint8_t xl = Wire.read();       // 0x04
    uint8_t yh = Wire.read();       // 0x05
    uint8_t yl = Wire.read();       // 0x06
    
    if (fingers > 0) {
      uint16_t rawX = ((xh & 0x0F) << 8) | xl;
      uint16_t rawY = ((yh & 0x0F) << 8) | yl;
      
      // Map for landscape rotation (rotation=1)
      screenX = rawY;
      screenY = SCREEN_HEIGHT - rawX;
      
      // Clamp to screen bounds
      screenX = constrain(screenX, 0, SCREEN_WIDTH - 1);
      screenY = constrain(screenY, 0, SCREEN_HEIGHT - 1);
      
      return true;
    }
  }
  return false;
#endif
}

// Reinitialize touch controller after audio playback
// This is needed on the resistive board because I2S internal DAC uses GPIO25
// which conflicts with the touch SPI clock
void reinitTouch() {
#if defined(BOARD_CYD_RESISTIVE)
  // Reinitialize the HSPI bus for touch
  touchSPI.end();
  delay(10);
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);
  Serial.println("Touch controller reinitialized");
#endif
  // Capacitive touch uses I2C, no conflict with I2S DAC
}

// ===== MAIN LOOP =====
void loop() {
  // Handle audio playback - must be called frequently!
  if (audioPlaying && wav != nullptr) {
    if (wav->isRunning()) {
      // Check if we should stop early (0.5 seconds before end to avoid trailing buzz)
      if (file != nullptr && wavStopPosition > 0 && file->getPos() >= wavStopPosition) {
        wav->stop();
        audioPlaying = false;
        resetPlayingButton();
        reinitTouch();  // Reinit touch after audio (I2S may have affected GPIO25)
        Serial.println("WAV playback stopped early (avoiding buzz)");
      } else if (!wav->loop()) {
        // Playback finished naturally
        wav->stop();
        audioPlaying = false;
        resetPlayingButton();
        reinitTouch();  // Reinit touch after audio (I2S may have affected GPIO25)
        Serial.println("WAV playback complete");
      }
    } else {
      audioPlaying = false;
      resetPlayingButton();
      reinitTouch();  // Reinit touch after audio
    }
  }

  // Poll touch at ~20Hz
  static unsigned long lastTouchRead = 0;
  static bool wasTouched = false;

  if (millis() - lastTouchRead > 50) {
    lastTouchRead = millis();

    int screenX, screenY;

    if (readTouch(screenX, screenY)) {
      if (!wasTouched) {  // New touch started
        wasTouched = true;

        unsigned long currentMillis = millis();
        if (currentMillis - lastTouchMillis >= TOUCH_DEBOUNCE_MS) {
          Serial.printf("TOUCH: (%d, %d)\n", screenX, screenY);
          handleTouch(screenX, screenY);
          lastTouchMillis = currentMillis;
        }
      }
    } else {
      wasTouched = false;
    }
  }

  // Small delay - audio loop handles timing
  delay(1);
}

// ===== UI DRAWING =====
void drawUI() {
  tft.fillScreen(COLOR_BLACK);
  drawHeader();
  drawSoundButtons();
  drawScrollIndicators();
}

void drawHeader() {
  // Title
  tft.setTextColor(COLOR_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.drawString("Sound Board", 10, 10);
  
  // Volume controls
  drawVolumeControls();
}

void drawVolumeControls() {
  // Minus button
  drawButton(VOL_MINUS_X, VOL_BTN_Y, VOL_BTN_SIZE, VOL_BTN_SIZE, "-", COLOR_DARKGRAY, COLOR_WHITE);
  
  // Plus button
  drawButton(VOL_PLUS_X, VOL_BTN_Y, VOL_BTN_SIZE, VOL_BTN_SIZE, "+", COLOR_DARKGRAY, COLOR_WHITE);
  
  // Volume number
  tft.fillRect(VOL_NUM_X - 15, VOL_BTN_Y, 30, VOL_BTN_SIZE, COLOR_BLACK);
  tft.setTextColor(COLOR_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  char volStr[4];
  snprintf(volStr, sizeof(volStr), "%d", volume);
  tft.drawString(volStr, VOL_NUM_X, VOL_NUM_Y);
}

void drawSoundButtons() {
  // Clear the button area
  tft.fillRect(0, LIST_TOP, SCREEN_WIDTH, LIST_HEIGHT, COLOR_BLACK);
  
  if (soundCount == 0) {
    // Show error message
    tft.setTextColor(COLOR_RED);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    if (!sdCardOk) {
      tft.drawString("No SD Card", SCREEN_WIDTH / 2, LIST_TOP + LIST_HEIGHT / 2 - 12);
    } else {
      tft.drawString("No sounds found", SCREEN_WIDTH / 2, LIST_TOP + LIST_HEIGHT / 2 - 12);
    }
    tft.setTextColor(COLOR_GRAY);
    tft.setTextSize(1);
    tft.drawString("Insert SD with index.csv", SCREEN_WIDTH / 2, LIST_TOP + LIST_HEIGHT / 2 + 12);
    return;
  }
  
  int y = LIST_TOP;
  for (int i = 0; i < VISIBLE_BUTTONS && (scrollOffset + i) < soundCount; i++) {
    int soundIndex = scrollOffset + i;
    drawButton(BUTTON_X, y, BUTTON_WIDTH, BUTTON_HEIGHT, 
               sounds[soundIndex].title, COLOR_BLUE, COLOR_WHITE);
    y += BUTTON_HEIGHT + BUTTON_MARGIN;
  }
}

void drawScrollIndicators() {
  int y = SCROLL_Y;
  
  // Up arrow (enabled if we can scroll up)
  uint16_t upColor = (scrollOffset > 0) ? COLOR_GREEN : COLOR_DARKGRAY;
  drawButton(SCROLL_UP_X, y, SCROLL_BTN_W, SCROLL_BTN_H, "^", upColor, COLOR_WHITE);
  
  // Down arrow (enabled if more items below)
  bool canScrollDown = (scrollOffset + VISIBLE_BUTTONS) < soundCount;
  uint16_t downColor = canScrollDown ? COLOR_GREEN : COLOR_DARKGRAY;
  drawButton(SCROLL_DOWN_X, y, SCROLL_BTN_W, SCROLL_BTN_H, "v", downColor, COLOR_WHITE);
  
  // Page indicator
  tft.setTextColor(COLOR_GRAY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  char pageStr[16];
  int currentPage = (scrollOffset / VISIBLE_BUTTONS) + 1;
  int totalPages = ((soundCount - 1) / VISIBLE_BUTTONS) + 1;
  snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage, totalPages);
  tft.drawString(pageStr, SCREEN_WIDTH / 2, y + SCROLL_BTN_H / 2);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t bgColor, uint16_t textColor) {
  tft.fillRoundRect(x, y, w, h, 6, bgColor);
  tft.drawRoundRect(x, y, w, h, 6, COLOR_WHITE);
  
  tft.setTextColor(textColor);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString(label, x + w/2, y + h/2);
}

// ===== SD CARD FUNCTIONS =====
bool initSDCard() {
  Serial.println("Initializing SD card...");
  Serial.printf("SD pins: CS=%d, MOSI=%d, MISO=%d, SCLK=%d\n", SD_CS, SD_MOSI, SD_MISO, SD_SCLK);
  
  // Initialize SPI bus for SD card
  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("ERROR: SD card mount failed!");
    sdCardOk = false;
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("ERROR: No SD card inserted!");
    sdCardOk = false;
    return false;
  }
  
  const char* cardTypeName = "UNKNOWN";
  if (cardType == CARD_MMC) cardTypeName = "MMC";
  else if (cardType == CARD_SD) cardTypeName = "SDSC";
  else if (cardType == CARD_SDHC) cardTypeName = "SDHC";
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card: %s, Size: %lluMB\n", cardTypeName, cardSize);
  
  sdCardOk = true;
  return true;
}

bool parseIndexCSV() {
  if (!sdCardOk) return false;
  
  File csvFile = SD.open("/index.csv", FILE_READ);
  if (!csvFile) {
    Serial.println("ERROR: Could not open /index.csv");
    return false;
  }
  
  Serial.println("Parsing index.csv...");
  // NOTE: Don't reset soundCount - beep is already at index 0
  int startCount = soundCount;  // Remember how many we started with (beep)
  bool headerSkipped = false;
  
  while (csvFile.available() && soundCount < MAX_SOUNDS) {
    String line = csvFile.readStringUntil('\n');
    line.trim();
    
    if (line.length() == 0) continue;
    
    // Skip header row
    if (!headerSkipped) {
      headerSkipped = true;
      Serial.printf("  Header: %s\n", line.c_str());
      continue;
    }
    
    // Parse CSV line: filename,title
    int commaPos = line.indexOf(',');
    if (commaPos <= 0) {
      Serial.printf("  Skipping invalid line: %s\n", line.c_str());
      continue;
    }
    
    String filename = line.substring(0, commaPos);
    String title = line.substring(commaPos + 1);
    filename.trim();
    title.trim();
    
    // Store in sounds array
    strncpy(sounds[soundCount].filename, filename.c_str(), sizeof(sounds[soundCount].filename) - 1);
    sounds[soundCount].filename[sizeof(sounds[soundCount].filename) - 1] = '\0';
    
    strncpy(sounds[soundCount].title, title.c_str(), sizeof(sounds[soundCount].title) - 1);
    sounds[soundCount].title[sizeof(sounds[soundCount].title) - 1] = '\0';
    
    Serial.printf("  [%d] %s -> %s\n", soundCount, sounds[soundCount].filename, sounds[soundCount].title);
    soundCount++;
  }
  
  csvFile.close();
  
  int loadedFromSD = soundCount - startCount;
  if (loadedFromSD == 0) {
    Serial.println("WARNING: No sounds found in index.csv (beep still available)");
    return false;
  }
  
  Serial.printf("Loaded %d sounds from SD card (total: %d with beep)\n", loadedFromSD, soundCount);
  return true;
}

// ===== SOUND PLAYBACK =====

// Add the beep as the first sound entry
void addBeepSound() {
  // Sound 0: Simple beep
  strncpy(sounds[0].filename, "BEEP", sizeof(sounds[0].filename) - 1);
  strncpy(sounds[0].title, "[Beep]", sizeof(sounds[0].title) - 1);
  
  // Sound 1: Police siren
  strncpy(sounds[1].filename, "SIREN", sizeof(sounds[1].filename) - 1);
  strncpy(sounds[1].title, "[Siren]", sizeof(sounds[1].title) - 1);
  
  // Sound 2: Success chime
  strncpy(sounds[2].filename, "CHIME", sizeof(sounds[2].filename) - 1);
  strncpy(sounds[2].title, "[Chime]", sizeof(sounds[2].title) - 1);
  
  // Sound 3: Laser zap
  strncpy(sounds[3].filename, "LASER", sizeof(sounds[3].filename) - 1);
  strncpy(sounds[3].title, "[Laser]", sizeof(sounds[3].title) - 1);
  
  soundCount = NUM_BUILTIN_SOUNDS;
  Serial.printf("Added %d built-in sounds\n", NUM_BUILTIN_SOUNDS);
}

// Helper: Play a tone at specified frequency and duration
void playTone(int freqHz, int durationMs, int vol) {
  if (freqHz <= 0 || vol <= 0) return;
  
  int amplitude = map(vol, 0, MAX_VOLUME, 0, 127);
  int halfPeriodUs = 500000 / freqHz;  // Half period in microseconds
  
  unsigned long startTime = millis();
  while (millis() - startTime < (unsigned long)durationMs) {
    dacWrite(SPEAKER_DAC_PIN, 128 + amplitude);
    delayMicroseconds(halfPeriodUs);
    dacWrite(SPEAKER_DAC_PIN, 128 - amplitude);
    delayMicroseconds(halfPeriodUs);
  }
}

// Play a simple beep
void playBeep() {
  Serial.printf("Playing beep at volume %d\n", volume);
  playTone(1000, 200, volume);
  dacWrite(SPEAKER_DAC_PIN, 128);  // Silence
}

// Play a police siren (rising and falling)
void playSiren() {
  Serial.printf("Playing siren at volume %d\n", volume);
  
  // Two cycles of rising/falling siren
  for (int cycle = 0; cycle < 2; cycle++) {
    // Rising: 400Hz -> 800Hz
    for (int freq = 400; freq <= 800; freq += 20) {
      playTone(freq, 15, volume);
    }
    // Falling: 800Hz -> 400Hz
    for (int freq = 800; freq >= 400; freq -= 20) {
      playTone(freq, 15, volume);
    }
  }
  dacWrite(SPEAKER_DAC_PIN, 128);  // Silence
}

// Play a success chime (ascending melody)
void playChime() {
  Serial.printf("Playing chime at volume %d\n", volume);
  
  // C-E-G-C (major arpeggio) - musical success sound
  int notes[] = {523, 659, 784, 1047};  // C5, E5, G5, C6
  int durations[] = {100, 100, 100, 250};
  
  for (int i = 0; i < 4; i++) {
    playTone(notes[i], durations[i], volume);
    delay(30);  // Small gap between notes
  }
  dacWrite(SPEAKER_DAC_PIN, 128);  // Silence
}

// Play a laser zap (descending sweep)
void playLaser() {
  Serial.printf("Playing laser at volume %d\n", volume);
  
  // Quick descending sweep from high to low
  for (int freq = 2000; freq >= 200; freq -= 50) {
    playTone(freq, 8, volume);
  }
  dacWrite(SPEAKER_DAC_PIN, 128);  // Silence
}

// Play a WAV file from SD card using ESP8266Audio library
bool playWavFile(const char* filename) {
  char filepath[32];
  snprintf(filepath, sizeof(filepath), "/%s", filename);

  Serial.printf("Opening WAV file: %s\n", filepath);

  // Stop any currently playing audio
  if (wav != nullptr && wav->isRunning()) {
    wav->stop();
  }

  // Clean up previous file source
  if (file != nullptr) {
    delete file;
    file = nullptr;
  }

  // Clean up previous generator
  if (wav != nullptr) {
    delete wav;
    wav = nullptr;
  }

  // Create new file source
  file = new AudioFileSourceSD(filepath);
  if (!file->isOpen()) {
    Serial.printf("ERROR: Could not open %s\n", filepath);
    delete file;
    file = nullptr;
    return false;
  }

  // Read WAV header to calculate early stop position (0.5 seconds before end)
  // WAV header structure: bytes 24-27 = sample rate, 34-35 = bits per sample
  // bytes 22-23 = number of channels
  uint8_t header[44];
  file->read(header, 44);
  file->seek(0, SEEK_SET);  // Reset to beginning for playback

  uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
  uint16_t numChannels = header[22] | (header[23] << 8);
  uint16_t bitsPerSample = header[34] | (header[35] << 8);
  uint32_t fileSize = file->getSize();

  // Calculate bytes per second: sampleRate * numChannels * (bitsPerSample / 8)
  uint32_t bytesPerSecond = sampleRate * numChannels * (bitsPerSample / 8);

  // Calculate position to stop (0.5 seconds = 500ms before end)
  uint32_t cutoffBytes = bytesPerSecond / 2;  // 0.5 seconds worth of bytes

  if (fileSize > cutoffBytes + 44) {  // Ensure we don't underflow (44 = header size)
    wavStopPosition = fileSize - cutoffBytes;
  } else {
    wavStopPosition = 0;  // File too short, play whole thing
  }

  Serial.printf("WAV: %d Hz, %d ch, %d bit, stop at %d/%d bytes\n",
                sampleRate, numChannels, bitsPerSample, wavStopPosition, fileSize);

  // Set volume based on current volume setting (0-10 -> 0.0-1.0 gain)
  float gain = (float)volume / (float)MAX_VOLUME;
  out->SetGain(gain);

  // Create WAV generator and start playback
  wav = new AudioGeneratorWAV();
  if (!wav->begin(file, out)) {
    Serial.println("ERROR: Could not start WAV playback");
    delete wav;
    wav = nullptr;
    delete file;
    file = nullptr;
    return false;
  }

  audioPlaying = true;
  Serial.println("WAV playback started");
  return true;
}

// Reset the currently playing button back to normal color
void resetPlayingButton() {
  if (currentlyPlayingIndex >= 0 && currentlyPlayingIndex < soundCount) {
    int visibleIndex = currentlyPlayingIndex - scrollOffset;
    if (visibleIndex >= 0 && visibleIndex < VISIBLE_BUTTONS) {
      int y = LIST_TOP + visibleIndex * (BUTTON_HEIGHT + BUTTON_MARGIN);
      drawButton(BUTTON_X, y, BUTTON_WIDTH, BUTTON_HEIGHT,
                 sounds[currentlyPlayingIndex].title, COLOR_BLUE, COLOR_WHITE);
    }
    currentlyPlayingIndex = -1;
  }
}

void playSound(int index) {
  if (index < 0 || index >= soundCount) return;

  // Reset any previously playing button
  resetPlayingButton();

  // Visual feedback - highlight the button
  int visibleIndex = index - scrollOffset;
  if (visibleIndex >= 0 && visibleIndex < VISIBLE_BUTTONS) {
    int y = LIST_TOP + visibleIndex * (BUTTON_HEIGHT + BUTTON_MARGIN);
    drawButton(BUTTON_X, y, BUTTON_WIDTH, BUTTON_HEIGHT,
               sounds[index].title, COLOR_GREEN, COLOR_WHITE);
  }

  // Check for built-in sounds by filename
  const char* filename = sounds[index].filename;
  bool isBuiltIn = false;

  if (strcmp(filename, "BEEP") == 0) {
    playBeep();
    isBuiltIn = true;
  } else if (strcmp(filename, "SIREN") == 0) {
    playSiren();
    isBuiltIn = true;
  } else if (strcmp(filename, "CHIME") == 0) {
    playChime();
    isBuiltIn = true;
  } else if (strcmp(filename, "LASER") == 0) {
    playLaser();
    isBuiltIn = true;
  } else {
    // Play WAV file from SD card (non-blocking)
    Serial.printf("Playing: %s (%s) at volume %d\n",
                  sounds[index].title, filename, volume);
    if (playWavFile(filename)) {
      // Track which button is playing for later reset
      currentlyPlayingIndex = index;
    } else {
      Serial.println("WAV playback failed!");
    }
  }

  // For built-in sounds (blocking), reset button immediately
  if (isBuiltIn) {
    if (visibleIndex >= 0 && visibleIndex < VISIBLE_BUTTONS) {
      int y = LIST_TOP + visibleIndex * (BUTTON_HEIGHT + BUTTON_MARGIN);
      drawButton(BUTTON_X, y, BUTTON_WIDTH, BUTTON_HEIGHT,
                 sounds[index].title, COLOR_BLUE, COLOR_WHITE);
    }
  }
}

// ===== TOUCH DETECTION =====
int getTouchedButton(int touchX, int touchY) {
  // Check if touch is in the button list area
  if (touchY >= LIST_TOP && touchY < LIST_TOP + LIST_HEIGHT) {
    for (int i = 0; i < VISIBLE_BUTTONS && (scrollOffset + i) < soundCount; i++) {
      int btnY = LIST_TOP + i * (BUTTON_HEIGHT + BUTTON_MARGIN);
      if (touchX >= BUTTON_X && touchX <= BUTTON_X + BUTTON_WIDTH &&
          touchY >= btnY && touchY <= btnY + BUTTON_HEIGHT) {
        return scrollOffset + i;
      }
    }
  }
  return -1;  // No button touched
}

// ===== TOUCH HANDLER =====
void handleTouch(int touchX, int touchY) {
  // Check volume minus
  if (touchX >= VOL_MINUS_X && touchX <= VOL_MINUS_X + VOL_BTN_SIZE &&
      touchY >= VOL_BTN_Y && touchY <= VOL_BTN_Y + VOL_BTN_SIZE) {
    if (volume > 0) {
      volume--;
      drawVolumeControls();
      Serial.printf("Volume: %d\n", volume);
    }
    return;
  }
  
  // Check volume plus
  if (touchX >= VOL_PLUS_X && touchX <= VOL_PLUS_X + VOL_BTN_SIZE &&
      touchY >= VOL_BTN_Y && touchY <= VOL_BTN_Y + VOL_BTN_SIZE) {
    if (volume < MAX_VOLUME) {
      volume++;
      drawVolumeControls();
      Serial.printf("Volume: %d\n", volume);
    }
    return;
  }
  
  // Check scroll up
  if (touchX >= SCROLL_UP_X && touchX <= SCROLL_UP_X + SCROLL_BTN_W &&
      touchY >= SCROLL_Y && touchY <= SCROLL_Y + SCROLL_BTN_H) {
    if (scrollOffset > 0) {
      scrollOffset -= VISIBLE_BUTTONS;
      if (scrollOffset < 0) scrollOffset = 0;
      drawSoundButtons();
      drawScrollIndicators();
      Serial.printf("Scroll up, offset: %d\n", scrollOffset);
    }
    return;
  }
  
  // Check scroll down
  if (touchX >= SCROLL_DOWN_X && touchX <= SCROLL_DOWN_X + SCROLL_BTN_W &&
      touchY >= SCROLL_Y && touchY <= SCROLL_Y + SCROLL_BTN_H) {
    if (scrollOffset + VISIBLE_BUTTONS < soundCount) {
      scrollOffset += VISIBLE_BUTTONS;
      drawSoundButtons();
      drawScrollIndicators();
      Serial.printf("Scroll down, offset: %d\n", scrollOffset);
    }
    return;
  }
  
  // Check sound buttons
  int buttonIndex = getTouchedButton(touchX, touchY);
  if (buttonIndex >= 0) {
    playSound(buttonIndex);
    return;
  }
  
  Serial.printf("Touch at (%d, %d) - no action\n", touchX, touchY);
}