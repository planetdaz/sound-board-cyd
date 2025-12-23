#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

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

#else
  #error "No board defined! Use -DBOARD_CYD_RESISTIVE or -DBOARD_CYD_CAPACITIVE"
#endif

// ===== DISPLAY CONFIGURATION =====
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// ===== COLOR DEFINITIONS =====
#define COLOR_RED     0xF800
#define COLOR_YELLOW  0xFFE0
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_WHITE   0xFFFF
#define COLOR_BLACK   0x0000
#define COLOR_GRAY    0x8410

// ===== GLOBAL OBJECTS =====
TFT_eSPI tft = TFT_eSPI();

// Touch debounce
const unsigned long TOUCH_DEBOUNCE_MS = 200;
unsigned long lastTouchMillis = 0;

// ===== FUNCTION DECLARATIONS =====
bool readTouch(int &screenX, int &screenY);
void handleTouch(int touchX, int touchY);
void drawUI();

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

// ===== MAIN LOOP =====
void loop() {
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
  
  // TODO: Add your main loop logic here
  
  delay(10);  // Small delay to prevent tight loop
}

// ===== UI DRAWING =====
void drawUI() {
  tft.fillScreen(COLOR_BLACK);
  
  // Title
  tft.setTextColor(COLOR_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(3);
  tft.drawString("Sound Board", SCREEN_WIDTH / 2, 20);
  
  // Board name
  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY);
  tft.drawString(BOARD_NAME, SCREEN_WIDTH / 2, 55);
  
  // Instructions
  tft.setTextSize(2);
  tft.setTextColor(COLOR_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Touch to test", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
  
  // TODO: Draw sound buttons here
}

// ===== TOUCH HANDLER =====
void handleTouch(int touchX, int touchY) {
  // Flash the touch location for visual feedback
  tft.fillCircle(touchX, touchY, 20, COLOR_GREEN);
  delay(100);
  
  // Redraw UI (simple approach - optimize later if needed)
  drawUI();
  
  // TODO: Check which button was pressed and play corresponding sound
  Serial.printf("Touch handled at: (%d, %d)\n", touchX, touchY);
}