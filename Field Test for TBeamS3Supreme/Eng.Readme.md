# LoRa Field Test — LilyGo T-Beam S3 Supreme

Standalone Meshtastic coverage tester for the **LilyGo T-Beam S3 Supreme** (ESP32-S3, SX1262, 1.3" 128×64 OLED, GPS, microSD, AXP2101).
This is **custom firmware, not Meshtastic**. It works on its own, with no second tester or phone: it measures the live Meshtastic network and logs every measurement with a GPS position to the SD card.

> Note: tested on a host (simulated radio, PMU, GPS, SD card and screen; the screen is drawn with the real u8g2 library) and compiled for ESP32-S3 (Arduino core 3.x, no warnings). **Not yet run on the board, and not built with the `espressif32@6.9.0` platform pinned in `platformio.ini`**, so the first flash may need small fixes.

## Screens
| Screen | Description |
|---|---|
| HOME | last received packet (node, RSSI, SNR, GOOD / MARG / WEAK rating), node and packet counts, airtime, noise, GPS position |
| MESH | Meshtastic node list: name, RSSI, SNR, hops, age; for the selected node **distance and bearing** (when it broadcasts a position and GPS has a fix) and an **ACK ping** |
| NOISE | real channel noise (radio RSSI 10×/s): now, floor, peak, busy, chart with 1 min / 10 min / 1 h windows |
| SCAN | 863–870 MHz sweep with peak hold; the triangle and dotted line mark the Meshtastic channel |
| GPS | position, altitude, satellites, HDOP, UTC, log state |
| SETUP | Meshtastic preset, ping power, auto ping, ping range, logging, GPS, contrast, flip; stored in NVS |

Header: `AP` auto ping on, `REC` logging to SD, `G8` GPS fix with 8 satellites (`G-` no fix), battery (`+` while charging), `TX` while transmitting, `RADIO!` on radio error.

Names and positions are decoded only on channels using the **default key** (AQ==). Message content is never shown or logged.
Rating = SNR margin over the demodulation limit for the preset's SF (SF7 −7.5 dB, 2.5 dB lower per SF step): ≥10 dB GOOD, ≥3 dB MARG, below WEAK.

### Ping
MESH: PWR short selects a node, PWR hold sends a want_ack packet (PRIVATE_APP port, own persistent random node ID). The result is ACK (RSSI, SNR, time, hops), NAK with error code, or no ACK after 12 s. **Ping hops**: `direct` (hop_limit 0, direct range only) or `up to 3` (relays allowed). **Auto ping** every 30 / 60 / 120 s, for walk tests. Only works on default-key channels; it transmits into the mesh, so use it sparingly.

### CSV log (microSD + serial)
Files `/ft0001.csv`, `/ft0002.csv`…; after power-on logging appends to the latest file. On the GPS screen PWR hold starts a new file and PWR short toggles logging. Without a card, rows go to serial only (115200).
```
ms,utc,lat,lon,alt_m,sats,hdop,event,node,name,rssi,snr,hops,dist_m,rtt_ms,noise_dbm,floor_dbm,busy_pct,info
```
Events: `RX`, `PING_ACK`, `PING_NAK`, `PING_TIMEOUT`, `PING_BLOCKED`, `NOISE` (every 10 s). Empty field = no data.

EU868 power and duty-cycle limits are enforced (869.4–869.65 MHz: 10 %).

## Buttons
| | BOOT (middle, GPIO0) | PWR (via AXP2101) |
|---|---|---|
| screens | short: next screen, hold: previous | short: screen action, hold ~1 s: second action |
| HOME | | short: ping, hold: clear node list |
| MESH | | short: next node, hold: ping |
| NOISE | | short: window, hold: clear |
| SCAN | | short: pause, hold: clear |
| GPS | | short: log on/off, hold: new log file |
| SETUP | short: next item, hold: exit | short: change, hold: previous item |

**Holding PWR for 6 s powers the board off** (AXP2101 hardware). About 1 s is enough for tester actions.

## Build
Open this folder in VS Code with PlatformIO, select `env:tbeam_supreme`, Build / Upload, or run `pio run -e tbeam_supreme -t upload`. Pinned: RadioLib 7.1.2, U8g2 2.36.18, XPowersLib 0.3.3, TinyGPSPlus 1.1.0, espressif32 6.9.0.

## Pins (Meshtastic variant `tbeam-s3-core`)
SX1262: CS 10, SCK 12, MOSI 11, MISO 13, RST 5, BUSY 4, DIO1 1, TCXO 1.8 V. OLED SH1106: SDA 17, SCL 18. AXP2101: SDA 42, SCL 41, IRQ 40. GPS: RX 9, TX 8, WAKE 7, 9600 baud. microSD: CS 47, SCK 36, MOSI 35, MISO 37 (IMU CS 34 held high). Power rails: ALDO1 OLED/sensors, ALDO2 sensor bus, ALDO3 LoRa, ALDO4 GPS, BLDO1 SD.

## Host tests
`./tools/host_test.sh`: unit tests plus a full firmware simulation (virtual SX1262, AXP2101, NMEA GPS, SD card, buttons, clock) that checks the CSV log and screen layout. It writes faithful OLED previews to `shots/*.png`.
