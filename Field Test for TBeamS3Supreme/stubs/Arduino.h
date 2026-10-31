#pragma once
// Zastepcze Arduino dla testow na hoscie (symulacja firmware bez plytki).
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <deque>
#include <math.h>
#include <algorithm>
#define constrain(a,lo,hi) ((a)<(lo)?(lo):((a)>(hi)?(hi):(a)))
#define radians(d) ((d)*M_PI/180.0)
#define degrees(r) ((r)*180.0/M_PI)
#define sq(x) ((x)*(x))
#ifndef TWO_PI
#define TWO_PI (2*M_PI)
#endif
using std::min;
using std::max;
typedef uint8_t byte;
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define SERIAL_8N1 0x800001c
unsigned long millis();void delay(unsigned long);void delayMicroseconds(unsigned);uint32_t esp_random();
void pinMode(int,int);void digitalWrite(int,int);int digitalRead(int);size_t strlcpy(char*,const char*,size_t);
struct SerialC{void begin(unsigned);int printf(const char*,...);void println(const char*);};extern SerialC Serial;
// UART GPS: test wstrzykuje zdania NMEA do kolejki
struct HardwareSerial{std::deque<char> in;unsigned long baud=0;int rx=-1,tx=-1;
 void begin(unsigned long b,uint32_t=0,int r=-1,int t=-1){baud=b;rx=r;tx=t;}int available(){return int(in.size());}int read(){if(in.empty())return -1;char c=in.front();in.pop_front();return (unsigned char)c;}
 void feed(const char* s){while(*s)in.push_back(*s++);}};
extern HardwareSerial Serial1;
