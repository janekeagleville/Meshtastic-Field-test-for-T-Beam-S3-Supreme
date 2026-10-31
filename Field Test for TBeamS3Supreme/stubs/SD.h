#pragma once
// Karta microSD w pamieci: pliki to napisy, test moze odczytac ich zawartosc.
#include <Arduino.h>
#include <SPI.h>
#include <map>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
struct SimSd{bool present=true,failWrites=false;std::map<std::string,std::string> files;int opens=0;};
extern SimSd gSd;
class File{std::string* f=nullptr;public:File(){}explicit File(std::string* p):f(p){}explicit operator bool()const{return f!=nullptr;}
 size_t print(const char* s){if(!f)return 0;f->append(s);return strlen(s);}size_t println(const char* s){size_t n=print(s);if(f)f->append("\n");return n+1;}
 size_t size()const{return f?f->size():0;}void close(){f=nullptr;}};
struct SDFS{bool begin(uint8_t,SPIClass&,uint32_t=4000000){return gSd.present;}bool exists(const char* p){return gSd.files.count(p)>0;}
 File open(const char* p,const char* m){++gSd.opens;if(!gSd.present||gSd.failWrites)return File();if(m[0]=='w')gSd.files[p].clear();return File(&gSd.files[p]);}};
extern SDFS SD;
