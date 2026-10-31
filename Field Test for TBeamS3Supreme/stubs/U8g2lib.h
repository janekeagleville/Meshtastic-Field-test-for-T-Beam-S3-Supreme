#pragma once
// U8g2 na hoscie: prawdziwa biblioteka C u8g2 (czcionki i rysowanie 1:1 z plytka) z wyswietlaczem w pamieci.
// Dodatkowo zapisywane sa prostokaty tekstow, zeby test mogl sprawdzic nachodzenie napisow i wyjscie poza ekran.
#include <Arduino.h>
#include <vector>
extern "C" {
#include "u8g2.h"
}
#define U8X8_PIN_NONE 255
#define U8G2_R0 (&u8g2_cb_r0)
#define U8G2_R2 (&u8g2_cb_r2)
struct TextBox{int x,y,w,h;std::string s;int color;};
class U8G2_SH1106_128X64_NONAME_F_HW_I2C{
 public:u8g2_t u;std::vector<TextBox> texts;int sends=0,contrast=-1;const u8g2_cb_t* rot=U8G2_R0;uint32_t bus=0;std::vector<uint8_t> frame;
 U8G2_SH1106_128X64_NONAME_F_HW_I2C(const u8g2_cb_t* r,uint8_t=U8X8_PIN_NONE,uint8_t=U8X8_PIN_NONE,uint8_t=U8X8_PIN_NONE):rot(r){u8g2_SetupBitmap(&u,r,128,64);u8g2_InitDisplay(&u);u8g2_SetPowerSave(&u,0);}
 bool begin(){return true;}void setBusClock(uint32_t c){bus=c;}void setContrast(uint8_t c){contrast=c;}void setPowerSave(uint8_t){}
 void setDisplayRotation(const u8g2_cb_t* r){rot=r;u8g2_SetDisplayRotation(&u,r);}
 void clearBuffer(){u8g2_ClearBuffer(&u);texts.clear();}
 // kopia bufora 128x64 (1 = piksel zapalony), w ukladzie ekranu niezaleznie od obrotu
 void sendBuffer(){++sends;frame.assign(128*64,0);uint8_t* b=u8g2_GetBufferPtr(&u);for(int y=0;y<64;y++)for(int x=0;x<128;x++)frame[y*128+x]=(b[(y/8)*128+x]>>(y%8))&1;}
 void setFont(const uint8_t* f){u8g2_SetFont(&u,f);}void setFontMode(uint8_t m){u8g2_SetFontMode(&u,m);}void setDrawColor(uint8_t c){u8g2_SetDrawColor(&u,c);}
 int getStrWidth(const char* s){return u8g2_GetStrWidth(&u,s);}int getAscent(){return u8g2_GetAscent(&u);}int getDescent(){return u8g2_GetDescent(&u);}
 int drawStr(int x,int y,const char* s){int w=u8g2_GetStrWidth(&u,s);int a=u8g2_GetAscent(&u),d=u8g2_GetDescent(&u);texts.push_back(TextBox{x,y-a,w,a-d,s,u8g2_GetDrawColor(&u)});return u8g2_DrawStr(&u,x,y,s);}
 void drawBox(int x,int y,int w,int h){u8g2_DrawBox(&u,x,y,w,h);}void drawFrame(int x,int y,int w,int h){u8g2_DrawFrame(&u,x,y,w,h);}
 void drawHLine(int x,int y,int w){u8g2_DrawHLine(&u,x,y,w);}void drawVLine(int x,int y,int h){u8g2_DrawVLine(&u,x,y,h);}
 void drawPixel(int x,int y){u8g2_DrawPixel(&u,x,y);}void drawLine(int a,int b,int c,int d){u8g2_DrawLine(&u,a,b,c,d);}
 void drawTriangle(int a,int b,int c,int d,int e,int f){u8g2_DrawTriangle(&u,a,b,c,d,e,f);}
 void drawDisc(int x,int y,int r){u8g2_DrawDisc(&u,x,y,r,U8G2_DRAW_ALL);}void drawCircle(int x,int y,int r){u8g2_DrawCircle(&u,x,y,r,U8G2_DRAW_ALL);}
};
