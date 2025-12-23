# CYD Sound Board

A touchscreen sound board for ESP32 "Cheap Yellow Display" (CYD) boards that plays WAV files from a micro SD card, plus built-in synthesized sound effects.

## Features

- **WAV File Playback:** Plays WAV files from micro SD card using ESP8266Audio library
- **Built-in Sound Effects:** 4 synthesized sounds always available (no SD card required):
  - **[Beep]** - Simple 1000Hz tone
  - **[Siren]** - Police siren (rising/falling sweep)
  - **[Chime]** - Success melody (C-E-G-C arpeggio)
  - **[Laser]** - Sci-fi laser zap (descending sweep)
- **Scrollable Button List:** Touch buttons for each sound, scrollable in landscape mode
- **Volume Control:** + and - buttons with current level indicator (0-10)
- **CSV-Based Sound Index:** Easy to customize sound titles via `index.csv`
- **Clean Touch UI:** Responsive touch interface with visual feedback

## Supported Hardware

This project supports **two CYD board variants** with automatic configuration via PlatformIO environments:

| Board | Display | Touch | Build Command |
|-------|---------|-------|---------------|
| ESP32-2432S028R | ILI9341 | XPT2046 (Resistive/SPI) | `pio run -e cyd_resistive` |
| JC2432W328C | ST7789 | CST816S (Capacitive/I2C) | `pio run -e cyd_capacitive` |

👉 **See [CYD_BOARD_COMPARISON.md](CYD_BOARD_COMPARISON.md) for detailed hardware differences and pin configurations.**

### Audio Output
- **DAC Pin:** GPIO 26 (internal 8-bit DAC)
- **Speaker Enable:** GPIO 4 (resistive board only, active LOW)
- **Amplifier:** FM8002E onboard amplifier
- **Speaker:** Connect to the 1.25mm 2P speaker connector

### Common Specifications
- **Display Size:** 2.8" 320×240 TFT LCD
- **CPU:** ESP32-WROOM-32 (Xtensa dual-core @ 240MHz)
- **Memory:** 4MB Flash, 520KB SRAM
- **Storage:** Micro SD card slot

## SD Card Setup

1. Format micro SD card as FAT32
2. Create an `index.csv` file in the root with your sound mappings:
   ```csv
   filename,title
   0001.wav,Achievement Bell
   0002.wav,Alert Bells Echo
   0003.wav,Arcade Score Interface
   ```
3. Add your WAV files to the root directory matching the filenames in `index.csv`

### Recommended WAV Format
For best compatibility and performance:
- **Sample Rate:** 8000-22050 Hz (lower = smoother playback)
- **Bit Depth:** 8-bit or 16-bit
- **Channels:** Mono preferred (stereo is mixed down)
- **Format:** PCM (uncompressed)

Convert with ffmpeg:
```bash
ffmpeg -i input.wav -ar 16000 -ac 1 output.wav
```

## UI Layout (Landscape 320×240)

```
+------------------------------------------+
|  Sound Board                    [-][+] 5 |
+------------------------------------------+
|  [ [Beep]                              ] |
|  [ [Siren]                             ] |
|  [ [Chime]                             ] |
|  [ [Laser]                             ] |
|  [ Achievement Bell                    ] |
|              [^]  1/3  [v]               |
+------------------------------------------+
```

- **Header:** App name with volume controls and current level
- **Button list:** Scrollable list of sounds (built-in + SD card)
- **Scroll controls:** Page up/down with page indicator

## Operation

1. Connect a speaker to the speaker connector
2. (Optional) Insert micro SD card with WAV files and `index.csv`
3. Power on the board via USB-C
4. Touch a button to play that sound
5. Use [-] / [+] to adjust volume (0-10)
6. Use [^] / [v] to scroll through sounds

## Building & Uploading

```bash
# For resistive touch board (ESP32-2432S028R)
pio run -e cyd_resistive -t upload -t monitor

# For capacitive touch board (JC2432W328C)
pio run -e cyd_capacitive -t upload -t monitor
```

## Libraries Used

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) - Display driver
- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) - Resistive touch
- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) - WAV playback

## Resources

- [Board Comparison Guide](CYD_BOARD_COMPARISON.md) - Detailed hardware differences
- [ESP32-2432S028R Documentation](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display) - LCD Wiki
- [JC2432W328C Board Definition](https://github.com/rzeldent/platformio-espressif32-sunton) - PlatformIO configs