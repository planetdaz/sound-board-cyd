# CYD Sound Board

A simple touchscreen sound board for ESP32 "Cheap Yellow Display" (CYD) boards that plays WAV files from a micro SD card.

## Purpose

Test and compare the audio capabilities of two different CYD ESP32 boards by playing WAV files via a simple touch interface.

## Features

- **WAV File Playback:** Plays numbered WAV files from micro SD card (0001.wav, 0002.wav, etc.)
- **Scrollable Button List:** Touch buttons for each sound file, scrollable in landscape mode
- **Volume Control:** + and - buttons with current level indicator
- **Simple UI:** Clean, easy-to-use touch interface

## Supported Hardware

This project supports **two CYD board variants** with automatic configuration via PlatformIO environments:

| Board | Display | Touch | Build Command |
|-------|---------|-------|---------------|
| ESP32-2432S028R | ILI9341 | XPT2046 (Resistive/SPI) | `pio run -e cyd_resistive` |
| JC2432W328C | ST7789 | CST816S (Capacitive/I2C) | `pio run -e cyd_capacitive` |

👉 **See [CYD_BOARD_COMPARISON.md](CYD_BOARD_COMPARISON.md) for detailed hardware differences and pin configurations.**

### Common Specifications
- **Display Size:** 2.8" 320×240 TFT LCD
- **CPU:** ESP32-WROOM-32 (Xtensa dual-core @ 240MHz)
- **Memory:** 4MB Flash, 520KB SRAM
- **Storage:** Micro SD card slot

## SD Card Setup

1. Format micro SD card as FAT32
2. Add WAV files to the root directory with numbered filenames:
   ```
   0001.wav
   0002.wav
   0003.wav
   ...
   ```
3. WAV files should be: 8-bit or 16-bit, mono or stereo, 8000-44100 Hz sample rate

## UI Layout (Landscape 320×240)

```
+------------------------------------------+
|  Sound Board                    [+][-] 5 |
+------------------------------------------+
|  [ 0001 ]  [ 0002 ]  [ 0003 ]            |
|  [ 0004 ]  [ 0005 ]  [ 0006 ]            |
|  [ 0007 ]  [ 0008 ]  [ 0009 ]            |
|                                          |
|              [▲ scroll ▼]                |
+------------------------------------------+
```

- **Title bar:** App name with volume controls (+ / -) and current level
- **Button grid:** Scrollable list of sound file buttons
- **Scroll controls:** Navigate through available sounds if more than fit on screen

## Operation

1. Insert micro SD card with numbered WAV files
2. Power on the board
3. Touch a numbered button to play that sound
4. Use + / - to adjust volume
5. Scroll up/down if you have more sounds than fit on screen

## Building

```bash
# For resistive touch board (ESP32-2432S028R)
pio run -e cyd_resistive -t upload

# For capacitive touch board (JC2432W328C)
pio run -e cyd_capacitive -t upload
```

## TODO

- [ ] SD card initialization and file scanning
- [ ] WAV file playback via DAC/I2S
- [ ] Scrollable button grid UI
- [ ] Volume control implementation
- [ ] Compare audio quality between the two boards

## Resources

- [Board Comparison Guide](CYD_BOARD_COMPARISON.md) - Detailed hardware differences
- [ESP32-2432S028R Documentation](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display) - LCD Wiki
- [JC2432W328C Board Definition](https://github.com/rzeldent/platformio-espressif32-sunton) - PlatformIO configs