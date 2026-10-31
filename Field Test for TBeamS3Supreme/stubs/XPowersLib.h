#pragma once
// Zastepczy AXP2101: stan kanalow zasilania, bateria i kolejka nacisniec przycisku PWR (1 = krotkie, 2 = przytrzymanie).
#include <Arduino.h>
#include <Wire.h>
#include <map>
#define AXP2101_SLAVE_ADDRESS 0x34
enum{XPOWERS_DCDC1,XPOWERS_DCDC2,XPOWERS_DCDC3,XPOWERS_DCDC4,XPOWERS_DCDC5,XPOWERS_ALDO1,XPOWERS_ALDO2,XPOWERS_ALDO3,XPOWERS_ALDO4,XPOWERS_BLDO1,XPOWERS_BLDO2};
enum{XPOWERS_AXP2101_CHG_CUR_500MA=9};enum{XPOWERS_AXP2101_CHG_VOL_4V2=3};enum{XPOWERS_POWEROFF_4S,XPOWERS_POWEROFF_6S};
enum xpowers_irq_time_t{XPOWERS_AXP2101_IRQ_TIME_1S,XPOWERS_AXP2101_IRQ_TIME_1S5,XPOWERS_AXP2101_IRQ_TIME_2S};
#define XPOWERS_AXP2101_PKEY_LONG_IRQ (1ull<<10)
#define XPOWERS_AXP2101_PKEY_SHORT_IRQ (1ull<<11)
#define XPOWERS_AXP2101_ALL_IRQ 0xFFFFFFFFull
struct SimPmu{bool present=true,batt=true,vbus=false,chg=false;int mv=3950,pct=78;std::map<int,bool> on;std::map<int,int> mvOut;std::deque<int> keys;int cur=0;uint64_t irqEn=0;int offTime=-1;};
extern SimPmu gPmu;
struct XPowersAXP2101{
 public:
 bool begin(TwoWire& w,uint8_t,int sda,int scl){w.begin(sda,scl);return gPmu.present;}
 bool setALDO1Voltage(uint16_t mv){gPmu.mvOut[XPOWERS_ALDO1]=mv;return true;}bool enableALDO1(){gPmu.on[XPOWERS_ALDO1]=true;return true;}bool disableALDO1(){gPmu.on[XPOWERS_ALDO1]=false;return true;}
 bool setALDO2Voltage(uint16_t mv){gPmu.mvOut[XPOWERS_ALDO2]=mv;return true;}bool enableALDO2(){gPmu.on[XPOWERS_ALDO2]=true;return true;}bool disableALDO2(){gPmu.on[XPOWERS_ALDO2]=false;return true;}
 bool setALDO3Voltage(uint16_t mv){gPmu.mvOut[XPOWERS_ALDO3]=mv;return true;}bool enableALDO3(){gPmu.on[XPOWERS_ALDO3]=true;return true;}bool disableALDO3(){gPmu.on[XPOWERS_ALDO3]=false;return true;}
 bool setALDO4Voltage(uint16_t mv){gPmu.mvOut[XPOWERS_ALDO4]=mv;return true;}bool enableALDO4(){gPmu.on[XPOWERS_ALDO4]=true;return true;}bool disableALDO4(){gPmu.on[XPOWERS_ALDO4]=false;return true;}
 bool setBLDO1Voltage(uint16_t mv){gPmu.mvOut[XPOWERS_BLDO1]=mv;return true;}bool enableBLDO1(){gPmu.on[XPOWERS_BLDO1]=true;return true;}bool disableBLDO1(){gPmu.on[XPOWERS_BLDO1]=false;return true;}
 bool disableDC2(){gPmu.on[XPOWERS_DCDC2]=false;return true;}bool disableDC5(){gPmu.on[XPOWERS_DCDC5]=false;return true;}
 void disableTSPinMeasure(){}bool enableBattDetection(){return true;}bool enableBattVoltageMeasure(){return true;}bool enableVbusVoltageMeasure(){return true;}bool enableSystemVoltageMeasure(){return true;}
 bool setChargerConstantCurr(uint8_t){return true;}bool setChargeTargetVoltage(uint8_t){return true;}
 void setIrqLevelTime(xpowers_irq_time_t){}bool setPowerKeyPressOffTime(uint8_t o){gPmu.offTime=o;return true;}
 bool disableIRQ(uint64_t m){gPmu.irqEn&=~m;return true;}bool enableIRQ(uint64_t m){gPmu.irqEn|=m;return true;}
 uint64_t getIrqStatus(){gPmu.cur=0;if(!gPmu.keys.empty()){gPmu.cur=gPmu.keys.front();gPmu.keys.pop_front();}return uint64_t(gPmu.cur);}
 bool isPekeyShortPressIrq(){return gPmu.cur==1&&(gPmu.irqEn&XPOWERS_AXP2101_PKEY_SHORT_IRQ);}bool isPekeyLongPressIrq(){return gPmu.cur==2&&(gPmu.irqEn&XPOWERS_AXP2101_PKEY_LONG_IRQ);}void clearIrqStatus(){gPmu.cur=0;}
 bool isVbusIn(){return gPmu.vbus;}bool isCharging(){return gPmu.chg;}bool isBatteryConnect(){return gPmu.batt;}uint16_t getBattVoltage(){return uint16_t(gPmu.mv);}int getBatteryPercent(){return gPmu.pct;}
 protected:   // jak w XPowersLib: ogolne funkcje kanalow nie sa publiczne
 bool setPowerChannelVoltage(uint8_t,uint16_t){return true;}bool enablePowerOutput(uint8_t){return true;}bool disablePowerOutput(uint8_t){return true;}
};
