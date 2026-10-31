#pragma once
#include <stdint.h>
// LilyGo T-Beam S3 Supreme (ESP32-S3, SX1262, OLED SH1106 1,3" 128x64, AXP2101, GPS L76K / MAX-M10S, czytnik microSD).
// Piny wg wariantu Meshtastic "tbeam-s3-core" (variant.h, pins_arduino.h).
constexpr int PIN_BTN_BOOT=0;                    // srodkowy przycisk BOOT (przytrzymany przy resecie wchodzi w tryb wgrywania)
constexpr int PIN_PMU_IRQ=40;                    // przerwanie AXP2101 (przycisk PWR), aktywne LOW
constexpr int PIN_PMU_SDA=42, PIN_PMU_SCL=41;    // I2C #1: AXP2101 + zegar PCF8563 (Wire1)
constexpr int PIN_OLED_SDA=17, PIN_OLED_SCL=18;  // I2C #0: OLED + czujniki (Wire)
constexpr int PIN_LORA_CS=10, PIN_LORA_SCK=12, PIN_LORA_MOSI=11, PIN_LORA_MISO=13, PIN_LORA_RST=5, PIN_LORA_BUSY=4, PIN_LORA_DIO1=1;
constexpr float LORA_TCXO_V=1.8f;                // TCXO zasilany z DIO3, DIO2 steruje przelacznikiem RF
constexpr int PIN_GPS_RX=9, PIN_GPS_TX=8;        // RX/TX po stronie ESP32
constexpr int PIN_GPS_WAKE=7;                    // HIGH = modul GPS aktywny
constexpr uint32_t GPS_BAUD=9600;
constexpr int PIN_SD_CS=47, PIN_SD_SCK=36, PIN_SD_MOSI=35, PIN_SD_MISO=37;
constexpr int PIN_IMU_CS=34;                     // IMU na tej samej szynie SPI co karta: trzymamy CS w stanie HIGH
constexpr int SCR_W=128, SCR_H=64;

// ---- Meshtastic ----
constexpr uint8_t MESH_SYNC=0x2B;
constexpr uint16_t MESH_PREAMBLE=16;
constexpr uint32_t PING_TIMEOUT_MS=12000;
// ---- Pomiar szumu ----
constexpr uint32_t NOISE_PERIOD_MS=100;          // chwilowy RSSI 10 razy na sekunde
// ---- Skan pasma ----
constexpr float SCAN_START=863.0f, SCAN_STEP=0.1f;
constexpr unsigned SCAN_N=71;                    // 863.0 ... 870.0 MHz
// ---- Log ----
constexpr uint32_t LOG_NOISE_MS=10000;           // wiersz NOISE co 10 s
