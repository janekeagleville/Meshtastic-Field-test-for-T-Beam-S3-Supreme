# LoRa Field Test — LilyGo T-Beam S3 Supreme

Samodzielny tester zasięgu sieci Meshtastic na płytce **LilyGo T-Beam S3 Supreme** (ESP32-S3, SX1262, OLED 1,3" 128×64, GPS, czytnik microSD, AXP2101).
To **własny firmware, nie Meshtastic**. Działa z jednym urządzeniem: nie potrzebuje drugiego testera ani telefonu. Mierzy na żywo istniejącą sieć Meshtastic i zapisuje pomiary z pozycją GPS na kartę.

> Uwaga: firmware został przetestowany na hoście (symulacja radia, PMU, GPS, karty i ekranu; ekran rysowany prawdziwą biblioteką u8g2). Skompilował się też dla ESP32-S3 (rdzeń Arduino 3.x, bez ostrzeżeń). **Nie był uruchamiany na płytce ani budowany na platformie `espressif32@6.9.0` z `platformio.ini`**, więc pierwsze wgranie może wymagać drobnych poprawek.

## Co potrafi

| Ekran | Opis |
|---|---|
| HOME | ostatni odebrany pakiet (węzeł, RSSI, SNR, ocena GOOD / MARG / WEAK), liczba węzłów i pakietów, zajętość eteru, szum, pozycja GPS |
| MESH | lista węzłów Meshtastic: nazwa, RSSI, SNR, hopy, wiek; dla wybranego węzła **odległość i kierunek** (gdy węzeł nadaje pozycję, a GPS ma fix) oraz **ping z potwierdzeniem ACK** |
| NOISE | **prawdziwy pomiar szumu** na kanale Meshtastic (chwilowy RSSI radia 10×/s): teraz, podłoga, szczyt, zajętość, wykres z oknami 1 min / 10 min / 1 h |
| SCAN | przemiatanie pasma 863–870 MHz (krok 100 kHz) z zapamiętaniem szczytów; trójkąt i kropkowana linia oznaczają kanał Meshtastic |
| GPS | pozycja, wysokość, satelity, HDOP, czas UTC, stan logu (plik, liczba wierszy) |
| SETUP | preset Meshtastic, moc pingu, auto ping, zasięg pingu, log, GPS, kontrast, obrót ekranu; zapis w NVS |

Nagłówek pokazuje: `AP` (auto ping włączony), `REC` (log na karcie), `G8` (GPS ma fix, 8 satelitów; `G-` brak fixu), stan baterii (z `+`, gdy się ładuje), `TX` w trakcie nadawania i `RADIO!` przy błędzie radia.

### Nasłuch sieci
Nagłówek pakietu Meshtastic jest jawny, więc bez klucza widać nadawcę, hopy, RSSI i SNR każdego pakietu. Jeśli kanał używa **domyślnego klucza** (AQ==), firmware odczytuje też nazwy węzłów (NODEINFO) i ich pozycje (POSITION). Treść wiadomości nie jest wyświetlana ani zapisywana.
Ocena GOOD / MARG / WEAK to margines SNR względem granicy demodulacji dla SF presetu (SF7 −7,5 dB, każdy kolejny SF o 2,5 dB mniej): ≥10 dB GOOD, ≥3 dB MARG, niżej WEAK.

### Ping węzła
1. Na ekranie MESH krótkie PWR wybiera węzeł, przytrzymanie PWR wysyła ping. Na HOME krótkie PWR pinguje wybrany (albo ostatnio słyszany) węzeł.
2. Tester wysyła do węzła pakiet z żądaniem potwierdzenia (want_ack) i portem PRIVATE_APP. Używa przy tym własnego, losowego i trwałego identyfikatora. Węzeł docelowy niczego nie wyświetla, tylko odsyła ACK.
3. Wynik: ACK z RSSI, SNR, czasem odpowiedzi i hopami, albo NAK z kodem błędu, albo brak ACK po 12 s. Licznik `potwierdzone/wysłane` jest osobny dla każdego węzła.
4. **Ping hops**: `direct` = hop_limit 0, test bezpośredniego zasięgu (pakiet nie jest przekazywany dalej). `up to 3` = ping może dojść przez przekaźniki.
5. **Auto ping** (30 / 60 / 120 s) pinguje wybrany węzeł cyklicznie. Do testu w terenie: chodzisz z T-Beamem, a każdy wynik trafia do logu z pozycją.
6. Ograniczenia: działa tylko dla kanału z domyślnym kluczem i nazwą zgodną z presetem. Ping to nadawanie w sieci, więc auto ping używaj z umiarem.

### Log CSV (karta microSD + port szeregowy)
Pliki `/ft0001.csv`, `/ft0002.csv`… Po włączeniu firmware dopisuje do ostatniego pliku. Przytrzymanie PWR na ekranie GPS zaczyna nowy plik, krótkie PWR włącza i wyłącza log. Bez karty wiersze idą tylko na port szeregowy (115200).

```
ms,utc,lat,lon,alt_m,sats,hdop,event,node,name,rssi,snr,hops,dist_m,rtt_ms,noise_dbm,floor_dbm,busy_pct,info
```
Zdarzenia: `RX` (każdy odebrany pakiet), `PING_ACK`, `PING_NAK`, `PING_TIMEOUT`, `PING_BLOCKED`, `NOISE` (co 10 s). Puste pole oznacza brak danych (np. brak fixu GPS albo pozycji węzła). Plik otwiera się bezpośrednio w Excelu, QGIS albo Google My Maps (kolumny lat/lon).

### Limity EU868 (pilnowane przez firmware)
Moc przycinana do limitu podpasma, czas nadawania z ostatniej godziny liczony i blokowany po przekroczeniu limitu wypełnienia (869,4–869,65 MHz: 10 %). Ping zablokowany limitem pokazuje „not sent: duty limit”.

## Obsługa
| | BOOT (środkowy, GPIO0) | PWR (przycisk zasilania, przez AXP2101) |
|---|---|---|
| zwykłe ekrany | krótko: następny ekran, długo: poprzedni | krótko: akcja ekranu, przytrzymanie ~1 s: druga akcja |
| HOME | | krótko: ping, długo: wyczyszczenie listy węzłów |
| MESH | | krótko: następny węzeł, długo: ping |
| NOISE | | krótko: okno 1 min / 10 min / 1 h, długo: wyczyszczenie |
| SCAN | | krótko: pauza, długo: wyczyszczenie |
| GPS | | krótko: log wł./wył., długo: nowy plik logu |
| SETUP | krótko: następna pozycja, długo: wyjście | krótko: zmiana wartości, długo: poprzednia pozycja |

**PWR przytrzymany 6 s wyłącza zasilanie** (sprzętowo, AXP2101). Dla akcji testera wystarczy ~1 s. Ponowne włączenie: krótko PWR.

## Budowanie (VS Code + PlatformIO)
Otwórz ten folder w VS Code (File → Open Folder), wybierz środowisko `env:tbeam_supreme` i kliknij Build / Upload. Z terminala:
```
pio run -e tbeam_supreme -t upload
pio device monitor
```
Zależności przypięte w `platformio.ini`: RadioLib 7.1.2, U8g2 2.36.18, XPowersLib 0.3.3, TinyGPSPlus 1.1.0, platforma espressif32 6.9.0.
Jeśli upload nie łapie portu: zamknij monitor portu, przytrzymaj BOOT, krótko naciśnij RST, puść BOOT i ponów Upload.

## Piny (z wariantu Meshtastic `tbeam-s3-core`)
LoRa SX1262: CS 10, SCK 12, MOSI 11, MISO 13, RST 5, BUSY 4, DIO1 1, TCXO 1,8 V, DIO2 jako przełącznik RF. OLED SH1106: I2C SDA 17, SCL 18. AXP2101: I2C SDA 42, SCL 41, IRQ 40. GPS: RX 9, TX 8, WAKE 7, 9600 bd. microSD: CS 47, SCK 36, MOSI 35, MISO 37 (IMU na tej samej szynie, CS 34 trzymany w HIGH). Przycisk BOOT: GPIO0.
Zasilanie z AXP2101: ALDO1 (OLED, czujniki), ALDO2 (magistrala czujników), ALDO3 (LoRa), ALDO4 (GPS), BLDO1 (karta).

## Do sprawdzenia na sprzęcie
- Czy płytka ma radio SX1262 (wersja 868 MHz). Wersje z SX1276/LR1121 nie są obsługiwane.
- Odczyt przycisku PWR przez przerwanie AXP2101 (czas przytrzymania 1 s).
- Moduł GPS (L76K albo MAX-M10S) powinien wysyłać NMEA na 9600 bd. Ekran GPS pokazuje liczbę odebranych znaków NMEA. Jeśli zostaje 0, sprawdź GPS w SETUP.
- Karta microSD sformatowana jako FAT32.
- Kontrast i orientacja ekranu (SETUP → Contrast, Flip).

## Testy na hoście
```
./tools/host_test.sh
```
`units` sprawdza AES, nagłówki, nazwy i pozycje Meshtastic, ping, ocenę sygnału, odległości, szum i limity wypełnienia. `sim` uruchamia cały firmware z wirtualnym SX1262, AXP2101, GPS (zdania NMEA), kartą SD, przyciskami i zegarem, sprawdza zawartość logu CSV i układ ekranów (brak nachodzących napisów). `shots/*.png` to wierne podglądy ekranu OLED.
