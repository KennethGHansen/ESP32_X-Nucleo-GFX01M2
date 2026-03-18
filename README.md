# ESP32‑S3 X‑NUCLEO‑GFX01M2 (ST7789H2) Driver

This project ports a custom STM32 HAL driver for the **X‑NUCLEO‑GFX01M2** display expansion board to the **ESP32‑S3‑DevKitC** using **ESP‑IDF (v5.x)**.

The driver targets the **ST7789H2** 2.2" SPI TFT display and reproduces the behavior of the original STM32 implementation, including text rendering and scaling.

---

## ✨ Features

- Native ESP‑IDF SPI master driver (`spi_master`)
- Uses SPI2 **IO_MUX default pins** (highest performance)
- RGB565 color format
- Fast full‑screen fill using DMA buffers
- Pixel, character, string, and scaled text drawing
- Newline (`\n`) handling in strings
- Clean ESP‑IDF component structure

---

## 🖥 Hardware

### Display
- **Board:** X‑NUCLEO‑GFX01M2
- **Controller:** ST7789H2
- **Resolution:** 240 × 320
- **Interface:** SPI

### MCU
- **Board:** ESP32‑S3‑DevKitC
- **Framework:** ESP‑IDF (v5.x)

---

## 📸 Screenshots
![20260318_143006](https://github.com/user-attachments/assets/2d702d88-d48f-4a40-86e7-73288c0999ee) 
![20260318_143015](https://github.com/user-attachments/assets/e125dcb6-b977-4cc6-864d-7436ee58fa0a)
![20260318_143023](https://github.com/user-attachments/assets/9a53201d-7118-41d4-9327-029519e88a16)

---

Example output from the demo application running on ESP32‑S3:
Hello World!
Just a Test
To see
If it works

---

## 🔌 Wiring Diagram

### ESP32‑S3 ↔ X‑NUCLEO‑GFX01M2 (LCD only)

| LCD Signal | ESP32‑S3 GPIO | Notes |
|-----------|---------------|------|
| CS        | GPIO10        | SPI2 CS0 (IO_MUX) |
| MOSI     | GPIO11        | SPI2 MOSI |
| SCLK     | GPIO12        | SPI2 SCLK |
| MISO     | GPIO13        | Optional (LCD write‑only) |
| DC       | GPIO9         | Data / Command |
| RST      | GPIO14        | Reset |
| VCC      | 3V3           | Power |
| GND      | GND           | Common ground |
| BCKL     | 3V3 or GPIO   | Backlight (board‑dependent) |

---

### Text Wiring Diagram
```
GPIO10   ───────────▶  LCD_CS
GPIO11   ───────────▶  LCD_MOSI
GPIO12   ───────────▶  LCD_SCK
GPIO9    ───────────▶  LCD_DC
GPIO14   ───────────▶  LCD_RST
3V3      ───────────▶  VCC
GND      ───────────▶  GND
```
> ✅ SPI2 IO_MUX pins are used for best signal integrity and speed.

---

## 🔄 Display Rotation & Orientation

The ST7789 uses the **MADCTL (0x36)** register to control rotation, mirroring, and color order.

In `st7789h2.c`:

```c
lcd_write_cmd(0x36);
uint8_t madctl = 0x40 | 0x08; // MX + BGR
lcd_write_data(&madctl, 1);
```

## MADCTL Bit Definitions (ST7789 / ST7789H2)

| Bit | Hex  | Name | Description |
|-----|------|------|-------------|
| 7   | 0x80 | MY   | Mirror Y axis (row address order) |
| 6   | 0x40 | MX   | Mirror X axis (column address order) |
| 5   | 0x20 | MV   | Swap X and Y axes (rotate 90°) |
| 4   | 0x10 | ML   | Vertical refresh order |
| 3   | 0x08 | BGR  | Color order: 1 = BGR, 0 = RGB |
| 2   | 0x04 | MH   | Horizontal refresh order |

## Common Display Rotation Settings (MADCTL values)

| Orientation | MADCTL Value | Notes |
|------------|--------------|-------|
| Portrait (default) | `0x40 \| 0x08` | MX + BGR |
| Portrait (180°) | `0x80 \| 0x08` | MY + BGR |
| Landscape (90° CW) | `0x20 \| 0x40 \| 0x08` | MV + MX + BGR |
| Landscape (90° CCW) | `0x20 \| 0x80 \| 0x08` | MV + MY + BGR |

✅ If colors look wrong, toggle the BGR (0x08) bit.
✅ If text is rotated or mirrored, adjust MX / MY / MV.

---
## Project Structure
```
ESP32_X-Nucleo-GFX01M2/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── main_app.c
└── components/
└── st7789h2/
├── CMakeLists.txt
├── include/
│   ├── st7789h2.h
│   └── font5x7.h
├── st7789h2.c
└── font5x7.c
```
---

## ⚙️ Notes

- Start with 10 MHz SPI clock, increase once stable
- Ensure shared ground between boards
- Backlight handling depends on X‑NUCLEO variant
- Driver uses polling SPI for reliability

---

## 📜 License
MIT License

---

## 👤 Author
Kenneth G. Hansen
STM32 → ESP32‑S3 driver port

---
