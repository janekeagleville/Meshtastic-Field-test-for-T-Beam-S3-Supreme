#!/usr/bin/env bash
# Testy na hoscie (bez plytki): testy jednostkowe + pelna symulacja z ekranem rysowanym prawdziwa biblioteka u8g2.
# Wymaga: g++, gcc, git, python3 z Pillow. Biblioteki u8g2 i TinyGPSPlus pobierane do .cache/ przy pierwszym uruchomieniu.
set -euo pipefail
cd "$(dirname "$0")/.."
C=.cache; mkdir -p "$C" shots
[ -d "$C/u8g2" ] || git clone -q --depth 1 --branch 2.36.18 https://github.com/olikraus/u8g2.git "$C/u8g2"
[ -d "$C/TinyGPSPlus" ] || git clone -q --depth 1 https://github.com/mikalhart/TinyGPSPlus.git "$C/TinyGPSPlus"
if [ ! -f "$C/libu8g2.a" ]; then
  mkdir -p "$C/u8obj"
  for f in "$C"/u8g2/csrc/*.c "$C"/u8g2/sys/bitmap/common/u8x8_d_bitmap.c; do gcc -O1 -c "$f" -I"$C/u8g2/csrc" -o "$C/u8obj/$(basename "$f" .c).o" & done; wait
  ar rcs "$C/libu8g2.a" "$C"/u8obj/*.o
fi
SAN="-fsanitize=address,undefined"
g++ -std=c++17 -O1 -g $SAN -DARDUINO=10819 -Istubs -Iinclude test/units.cpp -o "$C/units"
"$C/units" > /dev/null && echo "units OK"
g++ -std=c++17 -O1 -g $SAN -DARDUINO=10819 -Istubs -Iinclude -I"$C/u8g2/csrc" -I"$C/TinyGPSPlus/src" \
  test/sim.cpp "$C/TinyGPSPlus/src/TinyGPS++.cpp" "$C/libu8g2.a" -o "$C/sim"
ASAN_OPTIONS=detect_leaks=0 "$C/sim"
python3 tools/render.py shots --sheet && echo "podglady ekranow: shots/*.png, arkusz: shots/sheet.png"
