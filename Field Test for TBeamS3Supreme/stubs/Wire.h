#pragma once
#include <Arduino.h>
struct TwoWire{int sda=-1,scl=-1;bool begin(int d=-1,int c=-1){sda=d;scl=c;return true;}void setClock(uint32_t){}};
extern TwoWire Wire,Wire1;
