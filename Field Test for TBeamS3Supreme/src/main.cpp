// LoRa Field Test na LilyGo T-Beam S3 Supreme (ESP32-S3 + SX1262 + OLED 1,3" 128x64 + GPS + microSD + AXP2101).
// Samodzielny tester zasiegu sieci Meshtastic (NIE jest to firmware Meshtastic, nie potrzebuje drugiego urzadzenia):
//   - NASLUCH: pakiety Meshtastic (naglowki jawne): nadawca, RSSI, SNR, hopy; nazwy i pozycje wezlow dla domyslnego klucza kanalu
//   - PING: pakiet z zadaniem potwierdzenia do wybranego wezla, pomiar ACK (recznie lub automatycznie co 30/60/120 s)
//   - SZUM: chwilowy RSSI radia (10x/s), podloga, szczyty, zajetosc kanalu, okna 1 min / 10 min / 1 h
//   - SKAN: przemiatanie pasma 863-870 MHz z zapamietaniem szczytow
//   - GPS + LOG: kazdy odebrany pakiet, ping i pomiar szumu z pozycja GPS do pliku CSV na karcie microSD i na port szeregowy
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <XPowersLib.h>
#include <TinyGPS++.h>
#include "config.h"
#include "presets.h"
#include "noiselog.h"
#include "meshhdr.h"
#include "quality.h"
#include "geo.h"

#ifndef FW_VERSION
#define FW_VERSION "1.0"
#endif

// ================= Stan globalny =================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0,U8X8_PIN_NONE,PIN_OLED_SCL,PIN_OLED_SDA);
XPowersAXP2101 pmu;
TinyGPSPlus gps;
SPIClass loraSpi(FSPI);
SPIClass sdSpi(HSPI);
SX1262 radio=new Module(PIN_LORA_CS,PIN_LORA_DIO1,PIN_LORA_RST,PIN_LORA_BUSY,loraSpi);
Preferences prefs;

// Ustawienia (NVS). autoPing: 0 = wylaczony, 1..3 = co 30/60/120 s. pingHops: 0 = test bezposredni, 1 = do 3 przeskokow.
struct Cfg{uint8_t meshPreset=0,power=2,autoPing=0,log=1,gpsOn=1,contrast=2,flip=0,pingHops=0;} cfg;
enum Screen{S_HOME,S_MESH,S_NOISE,S_SCAN,S_GPS,S_SET,S_COUNT};
enum Mode{M_MESH,M_SCAN};
const char* SCR_NAME[S_COUNT]={"HOME","MESH","NOISE","SCAN","GPS","SETUP"};
int screen=S_HOME;Mode mode=M_MESH;bool dirty=true;uint32_t lastDraw=0,splashUntil=0;

struct Prof{float mhz,bw;uint8_t sf,cr,sync;uint16_t pre;int8_t pwr;};
Prof prof={869.525f,250,11,5,MESH_SYNC,MESH_PREAMBLE,14};
bool radioOk=false,pmuOk=false;int16_t rlErr=0;

pre::Duty duty;bool dutyBlocked=false;
nz::Log nzMesh;
mh::Table mtab;mh::Air meshAir;uint32_t crcErrors=0;
struct LastRx{uint32_t id=0,at=0;int rssi=0,hops=-1;float snr=0;bool has=false;} lastRx;
bool txBusy=false;uint32_t txDeadline=0;
uint32_t lastNoiseAt=0,lastBatAt=0,lastNoiseLogAt=0,lastAutoPingAt=0;
int batMv=0,batPct=-1;bool charging=false,vbus=false;
unsigned noiseWin=0;                       // 0 = 1 min, 1 = 10 min, 2 = 1 h
unsigned meshOff=0,setSel=0;uint32_t meshSelId=0,myId=0;bool scanPaused=false;
int8_t scanCur[SCAN_N],scanPeak[SCAN_N];unsigned scanIdx=0;

enum PingState{PG_IDLE,PG_WAIT,PG_ACK,PG_NAK,PG_TIMEOUT,PG_BLOCKED};
struct Ping{uint32_t target=0,id=0,sentAt=0;PingState st=PG_IDLE;unsigned sent=0,acks=0;int rssi=0,hops=-1;float snr=0;uint32_t rtt=0;uint8_t err=0;} pingSt;

// Pozycja wlasna z GPS
struct Fix{bool valid=false;double lat=0,lon=0,alt=0,hdop=0;unsigned sats=0;uint32_t age=0;char utc[24]="";char date[12]="";char time[10]="";} fix;
// Log CSV
struct LogState{bool card=false;char file[16]="";uint32_t rows=0,errors=0;} lg;

// ================= Radio =================
#define RL(x) do{int16_t e_=(x);if(e_!=RADIOLIB_ERR_NONE&&!rlErr)rlErr=e_;}while(0)
int txPower(){float f=pre::meshFreq(cfg.meshPreset);int pw=pre::linkPower(cfg.power);return pw>pre::powerLimit(f)?pre::powerLimit(f):pw;}
Prof profFor(Mode m){
 if(m==M_SCAN)return Prof{SCAN_START,125,7,5,MESH_SYNC,8,2};
 const pre::Preset& p=pre::get(cfg.meshPreset);return Prof{pre::meshFreq(cfg.meshPreset),p.bw,p.sf,p.cr,MESH_SYNC,MESH_PREAMBLE,int8_t(txPower())};}
void radioApply(const Prof& p){
 if(!radioOk)return;prof=p;txBusy=false;rlErr=0;
 RL(radio.standby());RL(radio.setBandwidth(p.bw));RL(radio.setSpreadingFactor(p.sf));RL(radio.setCodingRate(p.cr));RL(radio.setSyncWord(p.sync));RL(radio.setPreambleLength(p.pre));
 RL(radio.setFrequency(p.mhz));RL(radio.setOutputPower(p.pwr));RL(radio.startReceive());}
void switchMode(Mode m){mode=m;radioApply(profFor(m));lastNoiseAt=millis();}
void radioInit(){
 loraSpi.begin(PIN_LORA_SCK,PIN_LORA_MISO,PIN_LORA_MOSI,PIN_LORA_CS);
 Prof p=profFor(M_MESH);prof=p;
 int16_t e=radio.begin(p.mhz,p.bw,p.sf,p.cr,p.sync,p.pwr,p.pre,LORA_TCXO_V,false);
 if(e!=RADIOLIB_ERR_NONE){rlErr=e;radioOk=false;Serial.printf("# RADIO begin error %d\n",int(e));return;}
 radioOk=true;RL(radio.setDio2AsRfSwitch(true));RL(radio.setCurrentLimit(140.0f));RL(radio.setRxBoostedGainMode(true));RL(radio.startReceive());
 Serial.printf("# RADIO OK %.3f MHz SF%u BW%.1f\n",p.mhz,p.sf,p.bw);}
uint32_t toaMs(size_t len){return uint32_t(radio.getTimeOnAir(len)/1000)+1;}
bool txStart(const uint8_t* b,size_t n,uint32_t now){
 if(!radioOk||txBusy||mode!=M_MESH)return false;uint32_t air=toaMs(n);
 if(!duty.allow(now,air,pre::dutyLimit(prof.mhz))){dutyBlocked=true;return false;}
 int16_t e=radio.startTransmit(b,n);if(e!=RADIOLIB_ERR_NONE){rlErr=e;radio.startReceive();return false;}
 dutyBlocked=false;duty.add(now,air);txBusy=true;txDeadline=now+air+250;return true;}
void txPoll(uint32_t now){if(!txBusy)return;if(digitalRead(PIN_LORA_DIO1)||int32_t(now-txDeadline)>0){radio.finishTransmit();txBusy=false;radio.startReceive();}}

// ================= Zasilanie (AXP2101) =================
void gpsPower(bool on){if(pmuOk){if(on){pmu.setALDO4Voltage(3300);pmu.enableALDO4();}else pmu.disableALDO4();}
 digitalWrite(PIN_GPS_WAKE,on?HIGH:LOW);if(!on)fix=Fix();}
bool pmuInit(){
 if(!pmu.begin(Wire1,AXP2101_SLAVE_ADDRESS,PIN_PMU_SDA,PIN_PMU_SCL)){Serial.println("# PMU AXP2101 not found");return false;}
 // ALDO1: OLED i czujniki, ALDO2: magistrala czujnikow i RTC, ALDO3: LoRa, ALDO4: GPS, BLDO1: karta microSD
 pmu.setALDO1Voltage(3300);pmu.enableALDO1();
 pmu.setALDO2Voltage(3300);pmu.enableALDO2();
 pmu.setALDO3Voltage(3300);pmu.enableALDO3();
 pmu.setBLDO1Voltage(3300);pmu.enableBLDO1();
 pmu.disableDC2();pmu.disableDC5();
 pmu.disableTSPinMeasure();                       // brak termistora w akumulatorze: bez tego ladowanie moze sie nie wlaczyc
 pmu.enableBattDetection();pmu.enableBattVoltageMeasure();pmu.enableVbusVoltageMeasure();pmu.enableSystemVoltageMeasure();
 pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
 // PWR: krotkie nacisniecie i przytrzymanie 1 s to akcje testera; dopiero 6 s wylacza zasilanie
 pmu.setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_1S);pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
 pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);pmu.clearIrqStatus();pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ|XPOWERS_AXP2101_PKEY_LONG_IRQ);
 return true;}
void batRead(){if(!pmuOk){batPct=-1;return;}vbus=pmu.isVbusIn();charging=pmu.isCharging();
 if(pmu.isBatteryConnect()){batMv=pmu.getBattVoltage();batPct=pmu.getBatteryPercent();if(batPct>100)batPct=100;}else{batMv=0;batPct=-1;}}

// ================= GPS =================
void gpsPoll(){
 while(Serial1.available())gps.encode(char(Serial1.read()));
 if(!cfg.gpsOn)return;
 fix.sats=gps.satellites.isValid()?unsigned(gps.satellites.value()):0;
 fix.valid=gps.location.isValid()&&gps.location.age()<5000;
 if(fix.valid){fix.lat=gps.location.lat();fix.lon=gps.location.lng();fix.age=gps.location.age();}
 if(gps.altitude.isValid())fix.alt=gps.altitude.meters();
 if(gps.hdop.isValid())fix.hdop=gps.hdop.hdop();
 if(gps.date.isValid()&&gps.time.isValid()&&gps.date.year()>=2024){
  snprintf(fix.date,sizeof fix.date,"%04u-%02u-%02u",unsigned(gps.date.year()),unsigned(gps.date.month()),unsigned(gps.date.day()));
  snprintf(fix.time,sizeof fix.time,"%02u:%02u:%02u",unsigned(gps.time.hour()),unsigned(gps.time.minute()),unsigned(gps.time.second()));
  snprintf(fix.utc,sizeof fix.utc,"%sT%sZ",fix.date,fix.time);}}
// Odleglosc [m] do wezla z pozycja albo -1
double nodeDist(const mh::Node& n){if(!fix.valid||!n.hasPos)return -1;return geo::dist(fix.lat,fix.lon,n.lat*1e-7,n.lon*1e-7);}

// ================= Log CSV (karta microSD + port szeregowy) =================
const char* LOG_HEADER="ms,utc,lat,lon,alt_m,sats,hdop,event,node,name,rssi,snr,hops,dist_m,rtt_ms,noise_dbm,floor_dbm,busy_pct,info";
void logOpen(bool fresh){
 lg.card=false;lg.rows=0;lg.file[0]=0;
 if(!SD.begin(PIN_SD_CS,sdSpi,4000000)){Serial.println("# SD card not found, logging to serial only");return;}
 for(unsigned i=1;i<10000;i++){char p[16];snprintf(p,sizeof p,"/ft%04u.csv",i);if(!SD.exists(p)){
   if(!fresh&&i>1)snprintf(p,sizeof p,"/ft%04u.csv",i-1);   // bez 'fresh' dopisujemy do ostatniego pliku
   strlcpy(lg.file,p,sizeof lg.file);break;}}
 if(!lg.file[0])return;
 bool exists=SD.exists(lg.file);File f=SD.open(lg.file,FILE_APPEND);if(!f){Serial.println("# SD open failed");return;}
 if(!exists||f.size()==0)f.println(LOG_HEADER);f.close();lg.card=true;Serial.printf("# logging to %s\n",lg.file);}
// Pola puste, gdy brak danych. Nazwa wezla bez przecinkow.
void logRow(const char* ev,uint32_t node,const char* name,bool hasSig,int rssi,float snr,int hops,double distM,long rttMs,const char* info){
 if(!cfg.log)return;char row[256];size_t k=0;auto add=[&](const char* fmt,...){if(k>=sizeof row)return;va_list ap;va_start(ap,fmt);int w=vsnprintf(row+k,sizeof row-k,fmt,ap);va_end(ap);if(w>0)k+=size_t(w);};
 add("%lu,%s,",(unsigned long)millis(),fix.utc);
 if(fix.valid)add("%.6f,%.6f,%.0f,%u,%.1f,",fix.lat,fix.lon,fix.alt,fix.sats,fix.hdop);else add(",,,%u,,",fix.sats);
 add("%s,",ev);if(node)add("!%08lx,",(unsigned long)node);else add(",");
 char nm[24]="";if(name){strlcpy(nm,name,sizeof nm);for(char* c=nm;*c;c++)if(*c==','||*c=='"')*c=' ';}add("%s,",nm);
 if(hasSig)add("%d,%.2f,",rssi,snr);else add(",,");if(hops>=0)add("%d,",hops);else add(",");
 if(distM>=0)add("%.0f,",distM);else add(",");if(rttMs>=0)add("%ld,",rttMs);else add(",");
 nz::Stats s=nzMesh.stats(60);if(s.has)add("%d,%d,%.0f,",s.now,s.floor,s.busy);else add(",,,");add("%s",info?info:"");
 Serial.println(row);
 if(lg.card){File f=SD.open(lg.file,FILE_APPEND);if(f){f.println(row);f.close();++lg.rows;}else{++lg.errors;if(lg.errors>=3){lg.card=false;Serial.println("# SD write failed, card disabled");}}}}

// ================= Odbior pakietow =================
const char* nodeLabel(const mh::Node& n,char* b,size_t cap){if(n.name[0])snprintf(b,cap,"%s",n.name);else snprintf(b,cap,"!%08lx",(unsigned long)n.id);return b;}
void onMeshFrame(const uint8_t* b,size_t n,float rssi,float snr,uint32_t now){
 mh::Hdr h;if(!mh::parse(b,n,h))return;if(h.from==myId)return;
 if(pingSt.st==PG_WAIT&&h.from==pingSt.target&&h.to==myId){uint8_t er=0;if(mh::ackFor(b,n,h,pingSt.id,&er)){pingSt.rssi=int(lroundf(rssi));pingSt.snr=snr;pingSt.rtt=now-pingSt.sentAt;pingSt.hops=mh::hops(h);pingSt.err=er;
   if(er==0){pingSt.st=PG_ACK;++pingSt.acks;}else pingSt.st=PG_NAK;dirty=true;
   const mh::Node* t=mtab.find(h.from);char info[16];snprintf(info,sizeof info,"err=%u",unsigned(er));
   logRow(er==0?"PING_ACK":"PING_NAK",h.from,t?t->name:nullptr,true,pingSt.rssi,snr,pingSt.hops,t?nodeDist(*t):-1,long(pingSt.rtt),er?info:nullptr);}}
 mtab.add(h,b,n,int(lroundf(rssi)),snr,now);meshAir.add(now,uint32_t(radio.getTimeOnAir(n)));
 lastRx.has=true;lastRx.id=h.from;lastRx.at=now;lastRx.rssi=int(lroundf(rssi));lastRx.snr=snr;lastRx.hops=mh::hops(h);dirty=true;
 const mh::Node* x=mtab.find(h.from);char info[24];snprintf(info,sizeof info,"to=%08lx%s",(unsigned long)h.to,h.viaMqtt?" mqtt":"");
 logRow("RX",h.from,x?x->name:nullptr,true,lastRx.rssi,snr,lastRx.hops,x?nodeDist(*x):-1,-1,info);}
void rxPoll(uint32_t now){
 if(!radioOk||txBusy||mode!=M_MESH)return;if(!digitalRead(PIN_LORA_DIO1))return;
 uint8_t buf[256];size_t len=radio.getPacketLength();int16_t e=radio.readData(buf,len>sizeof buf?sizeof buf:len);float rssi=radio.getRSSI(),snr=radio.getSNR();radio.startReceive();
 if(e==RADIOLIB_ERR_CRC_MISMATCH){++crcErrors;return;}if(e!=RADIOLIB_ERR_NONE||!len||len>sizeof buf)return;
 onMeshFrame(buf,len,rssi,snr,now);}

// ================= Ping wezla =================
// Cel: wybrany wezel z listy, a gdy nie wybrano - ostatnio slyszany.
uint32_t pingTarget(uint32_t now){unsigned idx[mh::Table::MAXN];unsigned n=mtab.order(now,idx);for(unsigned i=0;i<n;i++)if(mtab.n[idx[i]].id==meshSelId)return meshSelId;return n?mtab.n[idx[0]].id:0;}
void pingSend(uint32_t now){
 if(!radioOk||mode!=M_MESH||pingSt.st==PG_WAIT)return;
 uint32_t target=pingTarget(now);if(!target)return;
 if(target!=pingSt.target){pingSt.sent=pingSt.acks=0;}
 pingSt.target=target;meshSelId=target;uint32_t id=esp_random();if(!id)id=1;uint8_t pkt[40];
 size_t len=mh::buildPing(pkt,sizeof pkt,target,myId,id,mtab.defHash,cfg.pingHops?3:0);
 pingSt.id=id;if(txStart(pkt,len,now)){pingSt.st=PG_WAIT;pingSt.sentAt=now;++pingSt.sent;Serial.printf("# PING to=%08lx id=%08lx\n",(unsigned long)target,(unsigned long)id);}
 else{pingSt.st=PG_BLOCKED;const mh::Node* t=mtab.find(target);logRow("PING_BLOCKED",target,t?t->name:nullptr,false,0,0,-1,-1,-1,dutyBlocked?"duty limit":"radio busy");}
 dirty=true;}
void pingTick(uint32_t now){
 if(pingSt.st==PG_WAIT&&!txBusy&&now-pingSt.sentAt>PING_TIMEOUT_MS){pingSt.st=PG_TIMEOUT;dirty=true;const mh::Node* t=mtab.find(pingSt.target);
  logRow("PING_TIMEOUT",pingSt.target,t?t->name:nullptr,false,0,0,-1,t?nodeDist(*t):-1,-1,nullptr);}
 static const uint32_t AP[4]={0,30000,60000,120000};
 if(cfg.autoPing&&mode==M_MESH&&pingSt.st!=PG_WAIT&&now-lastAutoPingAt>=AP[cfg.autoPing%4]){lastAutoPingAt=now;pingSend(now);}}

// ================= Skan pasma i szum =================
void scanReset(){memset(scanCur,nz::NONE,sizeof scanCur);memset(scanPeak,nz::NONE,sizeof scanPeak);scanIdx=0;}
void scanTick(){
 if(!radioOk||scanPaused)return;
 for(int k=0;k<4;k++){float f=SCAN_START+scanIdx*SCAN_STEP;RL(radio.setFrequency(f));RL(radio.startReceive());delay(2);float r=radio.getRSSI(false);int v=int(lroundf(r));if(v<-127)v=-127;if(v>0)v=0;
  scanCur[scanIdx]=int8_t(v);if(scanPeak[scanIdx]==nz::NONE||v>scanPeak[scanIdx])scanPeak[scanIdx]=int8_t(v);scanIdx=(scanIdx+1)%SCAN_N;}
 dirty=true;}
void noiseTick(uint32_t now){
 if(!radioOk||mode!=M_MESH||txBusy||now-lastNoiseAt<NOISE_PERIOD_MS)return;lastNoiseAt=now;
 nzMesh.add(now,radio.getRSSI(false));
 if(now-lastNoiseLogAt>=LOG_NOISE_MS){lastNoiseLogAt=now;if(nzMesh.stats(60).has)logRow("NOISE",0,nullptr,false,0,0,-1,-1,-1,nullptr);}}

// ================= Rysowanie (bufor 128x64, jeden transfer na klatke) =================
const uint8_t* F_TINY=u8g2_font_4x6_tr;
const uint8_t* F_SMALL=u8g2_font_5x7_tr;
const uint8_t* F_MED=u8g2_font_6x10_tr;
const uint8_t* F_BIG=u8g2_font_helvB12_tr;
int txt(int x,int y,const char* s,const uint8_t* f=F_SMALL){u8g2.setFont(f);return u8g2.drawStr(x,y,s);}
int txtR(int xr,int y,const char* s,const uint8_t* f=F_SMALL){u8g2.setFont(f);int w=u8g2.getStrWidth(s);u8g2.drawStr(xr-w,y,s);return w;}
void txtC(int cx,int y,const char* s,const uint8_t* f=F_SMALL){u8g2.setFont(f);int w=u8g2.getStrWidth(s);u8g2.drawStr(cx-w/2,y,s);}
// Skraca tekst do szerokosci w pikselach.
void fit(char* s,int maxW,const uint8_t* f){u8g2.setFont(f);size_t n=strlen(s);while(n>1&&int(u8g2.getStrWidth(s))>maxW)s[--n]=0;}
// Odwrocona etykieta (tekst na bialym tle)
int chip(int x,int y,const char* s){u8g2.setFont(F_SMALL);int w=u8g2.getStrWidth(s)+3;u8g2.drawBox(x,y-7,w,9);u8g2.setDrawColor(0);u8g2.drawStr(x+2,y,s);u8g2.setDrawColor(1);return w;}
void dottedH(int x0,int x1,int y){for(int x=x0;x<=x1;x+=3)u8g2.drawPixel(x,y);}
void dottedV(int x,int y0,int y1){for(int y=y0;y<=y1;y+=2)u8g2.drawPixel(x,y);}
// Logo: trzy polaczone wezly sieci
void logo(int x,int y){u8g2.drawLine(x+2,y+5,x+6,y+1);u8g2.drawLine(x+6,y+1,x+10,y+6);const int nx[3]={x+2,x+6,x+10},ny[3]={y+5,y+1,y+6};
 for(int i=0;i<3;i++)u8g2.drawDisc(nx[i],ny[i],1);}
const char* ageText(uint32_t ms,char* b,size_t n){uint32_t s=ms/1000;if(s<100)snprintf(b,n,"%lus",(unsigned long)s);else if(s<6000)snprintf(b,n,"%lum",(unsigned long)(s/60));else snprintf(b,n,"%luh",(unsigned long)(s/3600));return b;}
unsigned meshSf(){return pre::get(cfg.meshPreset).sf;}

void drawHeader(){
 u8g2.drawBox(0,0,SCR_W,10);u8g2.setDrawColor(0);
 logo(1,1);txt(15,8,SCR_NAME[screen]);
 // bateria
 const int bx=114,by=2;u8g2.drawFrame(bx,by,11,6);u8g2.drawBox(bx+11,by+2,1,2);
 if(batPct>=0){int w=(batPct*9+50)/100;if(w>0)u8g2.drawBox(bx+1,by+1,w,4);}
 if(charging||(vbus&&batPct<0)){u8g2.setDrawColor(2);u8g2.drawHLine(bx+3,by+3,5);u8g2.drawVLine(bx+5,by+1,4);u8g2.setDrawColor(0);}
 int xr=111;char b[12];
 if(cfg.gpsOn){if(fix.valid)snprintf(b,sizeof b,"G%u",fix.sats);else snprintf(b,sizeof b,"G-");xr-=txtR(xr,8,b)+3;}
 if(cfg.log&&lg.card)xr-=txtR(xr,8,"REC")+3;
 if(cfg.autoPing)xr-=txtR(xr,8,"AP")+3;
 if(!radioOk)txtR(xr,8,"RADIO!");else if(txBusy)txtR(xr,8,"TX");
 u8g2.setDrawColor(1);}

void drawHome(){
 uint32_t now=millis();char b[40];
 // lewa kolumna: ostatni odebrany pakiet
 if(lastRx.has){const mh::Node* n=mtab.find(lastRx.id);char nm[28];if(n)nodeLabel(*n,nm,sizeof nm);else snprintf(nm,sizeof nm,"!%08lx",(unsigned long)lastRx.id);fit(nm,62,F_SMALL);txt(0,18,nm);
  snprintf(b,sizeof b,"%d",lastRx.rssi);int w=txt(0,32,b,F_BIG);txt(w+2,32,"dBm");
  ql::Q q=ql::quality(true,lastRx.snr,meshSf());snprintf(b,sizeof b,"%.1fdB %s",lastRx.snr,ql::text(q));txt(0,43,b);}
 else{txt(0,18,"LAST RX");txt(0,32,"--",F_BIG);txt(0,43,"listening...");}
 u8g2.drawVLine(64,12,33);
 // prawa kolumna: siec
 txt(67,18,"NODES");snprintf(b,sizeof b,"%u",mtab.used);txt(67,32,b,F_BIG);
 snprintf(b,sizeof b,"pk%lu",(unsigned long)mtab.packets);txtR(127,32,b);
 snprintf(b,sizeof b,"air %.1f%%",meshAir.pct(now));txt(67,43,b);
 u8g2.drawHLine(0,46,SCR_W);
 nz::Stats s=nzMesh.stats(60);
 if(s.has&&s.seconds>=10)snprintf(b,sizeof b,"Noise %d fl%d b%.0f%%",s.now,s.floor,s.busy);else if(s.has)snprintf(b,sizeof b,"Noise %d (measuring)",s.now);else snprintf(b,sizeof b,"Noise: measuring...");txt(0,53,b);
 if(!cfg.gpsOn)snprintf(b,sizeof b,"GPS off");
 else if(fix.valid)snprintf(b,sizeof b,"%u %.5f,%.5f",fix.sats,fix.lat,fix.lon);
 else snprintf(b,sizeof b,"GPS searching, %u sat",fix.sats);
 txt(0,62,b);}

void drawMesh(){
 uint32_t now=millis();char b[48];const pre::Preset& pr=pre::get(cfg.meshPreset);
 unsigned idx[mh::Table::MAXN];unsigned n=mtab.order(now,idx);
 snprintf(b,sizeof b,"%s %.3f",pr.name,(double)pre::meshFreq(cfg.meshPreset));txt(0,18,b);
 if(!n){txtC(64,34,"listening for",F_SMALL);txtC(64,43,"Meshtastic packets",F_SMALL);snprintf(b,sizeof b,"crc err %lu",(unsigned long)crcErrors);txtC(64,58,b,F_TINY);return;}
 unsigned sel=0;bool found=false;for(unsigned i=0;i<n;i++)if(mtab.n[idx[i]].id==meshSelId){sel=i;found=true;}if(!found){meshSelId=mtab.n[idx[0]].id;sel=0;}
 snprintf(b,sizeof b,"%u/%u",sel+1,n);txtR(127,18,b);
 constexpr unsigned ROWS=3;
 if(sel<meshOff)meshOff=sel;if(sel>=meshOff+ROWS)meshOff=sel-(ROWS-1);if(meshOff+ROWS>n)meshOff=n>ROWS?n-ROWS:0;
 for(unsigned r=0;r<ROWS&&meshOff+r<n;r++){const mh::Node& x=mtab.n[idx[meshOff+r]];int y=28+int(r)*9;bool isSel=meshOff+r==sel;
  if(isSel){u8g2.drawBox(0,y-7,SCR_W,9);u8g2.setDrawColor(0);}
  char nm[28];nodeLabel(x,nm,sizeof nm);fit(nm,52,F_SMALL);txt(1,y,nm);
  snprintf(b,sizeof b,"%d",x.rssi);txtR(77,y,b);snprintf(b,sizeof b,"%d",int(lroundf(x.snr)));txtR(95,y,b);
  if(x.hops>=0)snprintf(b,sizeof b,"h%d",x.hops);else snprintf(b,sizeof b,"h?");txtR(109,y,b);
  ageText(now-x.last,b,sizeof b);txtR(127,y,b);u8g2.setDrawColor(1);}
 u8g2.drawHLine(0,47,SCR_W);
 // szczegoly wybranego wezla: odleglosc i liczniki pingu
 const mh::Node& s=mtab.n[idx[sel]];double d=nodeDist(s);char dd[12];
 if(d>=0)snprintf(b,sizeof b,"%s %s",geo::fmtDist(d,dd,sizeof dd),geo::compass(geo::bearing(fix.lat,fix.lon,s.lat*1e-7,s.lon*1e-7)));
 else if(s.hasPos)snprintf(b,sizeof b,"pos ok, no GPS fix");else snprintf(b,sizeof b,"%lu pk, no pos",(unsigned long)s.count);
 txt(0,55,b);
 bool mine=pingSt.target&&pingSt.target==s.id;
 if(mine){snprintf(b,sizeof b,"%u/%u",pingSt.acks,pingSt.sent);txtR(127,55,b);}
 if(mine){switch(pingSt.st){
   case PG_WAIT:snprintf(b,sizeof b,"PING... %lus",(unsigned long)((now-pingSt.sentAt)/1000));break;
   case PG_ACK:snprintf(b,sizeof b,"ACK %d %.1fdB %.1fs",pingSt.rssi,pingSt.snr,pingSt.rtt/1000.0);break;
   case PG_NAK:snprintf(b,sizeof b,"NAK err %u %d dBm",unsigned(pingSt.err),pingSt.rssi);break;
   case PG_TIMEOUT:snprintf(b,sizeof b,"no ACK in %lus",(unsigned long)(PING_TIMEOUT_MS/1000));break;
   case PG_BLOCKED:snprintf(b,sizeof b,dutyBlocked?"not sent: duty limit":"not sent: radio busy");break;
   default:snprintf(b,sizeof b,"hold PWR: ping");}
  txt(0,63,b);}
 else txt(0,63,"hold PWR: ping node");}

void drawNoise(){
 const nz::Log& L=nzMesh;static const unsigned W[3]={60,600,3600};static const char* WN[3]={"1m","10m","1h"};unsigned win=W[noiseWin%3];nz::Stats s=L.stats(win);char b[40];
 if(s.has){snprintf(b,sizeof b,"Now %d  Floor %d",s.now,s.floor);txt(0,18,b);if(s.seconds>=10)snprintf(b,sizeof b,"Peak %d  Busy %.0f%%",s.peak,s.busy);else snprintf(b,sizeof b,"Peak %d  Busy --",s.peak);txt(0,26,b);}
 else{txt(0,18,"Noise: measuring...");}
 txtR(127,26,WN[noiseWin%3]);
 const int x0=17,x1=127,yt=29,yb=62,w=x1-x0+1;
 int lo=-130,hi=-60;if(s.has){lo=((s.floor-10)/10)*10-(s.floor-10<0?10:0);if(lo<-130)lo=-130;hi=((s.peak+5)/10)*10+10;if(hi>-30)hi=-30;if(hi-lo<30)hi=lo+30;}
 auto Y=[&](int v){return yb-(constrain(v,lo,hi)-lo)*(yb-yt)/(hi-lo);};
 snprintf(b,sizeof b,"%d",hi);txtR(15,yt+5,b,F_TINY);snprintf(b,sizeof b,"%d",lo);txtR(15,yb,b,F_TINY);
 for(int v=lo+20;v<hi;v+=20)dottedH(x0,x1,Y(v));
 u8g2.drawVLine(x0-1,yt,yb-yt+1);
 for(int cx=0;cx<w;cx++){unsigned a0=unsigned(uint64_t(w-1-cx)*win/w),a1=unsigned(uint64_t(w-cx)*win/w);if(a1<=a0)a1=a0+1;if(a0>=L.used)continue;if(a1>L.used)a1=L.used;
  int mn=0,mx=-200;long sum=0;unsigned k=0;bool any=false;for(unsigned a=a0;a<a1;a++){int i=L.idx(a);if(L.avg[i]==nz::NONE)continue;if(!any){mn=L.lo[i];any=true;}mn=std::min<int>(mn,L.lo[i]);mx=std::max<int>(mx,L.hi[i]);sum+=L.avg[i];++k;}
  if(!any)continue;(void)mn;int x=x0+cx;int ya=Y(int(sum/long(k))),ymx=Y(mx);
  // slupek = sredni poziom, kropka = szczyt (gdy wyraznie wyzej)
  u8g2.drawVLine(x,ya,yb-ya+1);if(ya-ymx>=3)u8g2.drawPixel(x,ymx);}}

void drawScan(){
 char b[40];const int x0=12,x1=127,yt=21,yb=55;
 auto Y=[&](int v){return yb-(constrain(v,-130,-40)+130)*(yb-yt)/90;};
 auto X=[&](float f){return x0+int(lroundf((f-SCAN_START)/(SCAN_STEP*(SCAN_N-1))*(x1-x0)));};
 int cur=int((scanIdx+SCAN_N-1)%SCAN_N);
 if(scanCur[cur]!=nz::NONE)snprintf(b,sizeof b,"%.1f %ddBm",SCAN_START+cur*SCAN_STEP,scanCur[cur]);else snprintf(b,sizeof b,"sweeping...");txt(0,18,b);
 if(scanPaused)txtR(127,18,"PAUSED");
 else{int pk=nz::NONE,pi=0;for(unsigned i=0;i<SCAN_N;i++)if(scanPeak[i]!=nz::NONE&&scanPeak[i]>pk){pk=scanPeak[i];pi=int(i);}
  if(pk!=nz::NONE){snprintf(b,sizeof b,"pk %.1f",SCAN_START+pi*SCAN_STEP);txtR(127,18,b);}}
 for(int v=-120;v<=-60;v+=20)dottedH(x0,x1,Y(v));
 txtR(x0-1,Y(-60)+3,"60",F_TINY);txtR(x0-1,Y(-120)+3,"120",F_TINY);
 const float fm=pre::meshFreq(cfg.meshPreset);const int xm=X(fm);dottedV(xm,yt+3,yb);u8g2.drawTriangle(xm-2,yt,xm+2,yt,xm,yt+2);   // kanal Meshtastic
 for(unsigned i=0;i<SCAN_N;i++){int x=X(SCAN_START+i*SCAN_STEP);
  if(scanCur[i]!=nz::NONE){int y=Y(scanCur[i]);u8g2.drawVLine(x,y,yb-y+1);}
  if(scanPeak[i]!=nz::NONE){int y=Y(scanPeak[i]);u8g2.drawPixel(x,y);if(y>yt)u8g2.drawPixel(x,y-1);}}
 u8g2.drawHLine(x0,yb+1,x1-x0+1);
 for(int f=863;f<=870;f+=2){int x=X(float(f));u8g2.drawVLine(x,yb+2,2);snprintf(b,sizeof b,"%d",f);if(f==863)txt(x,63,b,F_TINY);else txtC(x,63,b,F_TINY);}
}

void drawGps(){
 char b[40];
 if(!cfg.gpsOn){txtC(64,30,"GPS is off",F_MED);txtC(64,42,"turn on in SETUP");}
 else if(fix.valid){
  snprintf(b,sizeof b,"%c %.6f",fix.lat>=0?'N':'S',fabs(fix.lat));txt(0,21,b,F_MED);snprintf(b,sizeof b,"%u sat",fix.sats);txtR(127,21,b);
  snprintf(b,sizeof b,"%c %.6f",fix.lon>=0?'E':'W',fabs(fix.lon));txt(0,32,b,F_MED);snprintf(b,sizeof b,"h%.1f",fix.hdop);txtR(127,32,b);
  snprintf(b,sizeof b,"Alt %.0fm",fix.alt);txt(0,41,b);if(fix.time[0]){snprintf(b,sizeof b,"%s UTC",fix.time);txtR(127,41,b);}
  if(fix.date[0])txt(0,50,fix.date);}
 else{txt(0,21,"No fix",F_MED);snprintf(b,sizeof b,"searching, %u sat",fix.sats);txt(0,32,b);
  snprintf(b,sizeof b,"NMEA chars %lu",(unsigned long)gps.charsProcessed());txt(0,41,b);if(gps.charsProcessed()<10&&millis()>5000)txt(0,50,"no data from module");}
 u8g2.drawHLine(0,53,SCR_W);
 if(!cfg.log)snprintf(b,sizeof b,"LOG off");
 else if(lg.card)snprintf(b,sizeof b,"LOG %s %lu",lg.file+1,(unsigned long)lg.rows);
 else snprintf(b,sizeof b,"LOG serial only (no SD)");
 txt(0,62,b);}

// ---- Ustawienia ----
constexpr unsigned SET_N=8;
void setLine(unsigned i,char* name,size_t nc,char* val,size_t vc){
 static const char* N[SET_N]={"Preset","TX power","Auto ping","Ping hops","Log","GPS","Contrast","Flip"};
 snprintf(name,nc,"%s",N[i]);
 switch(i){
  case 0:snprintf(val,vc,"%s",pre::get(cfg.meshPreset).name);break;
  case 1:snprintf(val,vc,"%d dBm%s",txPower(),pre::linkPower(cfg.power)>txPower()?"*":"");break;
  case 2:{static const char* A[4]={"OFF","30 s","60 s","120 s"};snprintf(val,vc,"%s",A[cfg.autoPing%4]);}break;
  case 3:snprintf(val,vc,"%s",cfg.pingHops?"up to 3":"direct");break;
  case 4:snprintf(val,vc,"%s",cfg.log?"ON":"OFF");break;
  case 5:snprintf(val,vc,"%s",cfg.gpsOn?"ON":"OFF");break;
  case 6:{static const char* C[4]={"low","mid","high","max"};snprintf(val,vc,"%s",C[cfg.contrast%4]);}break;
  default:snprintf(val,vc,"%s",cfg.flip?"180 deg":"normal");}}
void drawSet(){
 constexpr unsigned ROWS=5;unsigned first=setSel<2?0:setSel-2;if(first+ROWS>SET_N)first=SET_N-ROWS;
 for(unsigned r=0;r<ROWS;r++){unsigned i=first+r;int y=19+int(r)*9;bool sel=i==setSel;char nm[16],v[24];setLine(i,nm,sizeof nm,v,sizeof v);
  if(sel){u8g2.drawBox(0,y-7,SCR_W,9);u8g2.setDrawColor(0);}txt(2,y,nm);txtR(126,y,v);u8g2.setDrawColor(1);}
 txt(0,63,"BOOT:next PWR:change hold:exit",F_TINY);}

void drawSplash(){
 logo(4,3);txt(20,10,"LoRa Field Test");txtC(64,26,"T-Beam S3 Supreme",F_MED);
 txtC(64,37,"BOOT: screen  PWR: action");
 char b[40];snprintf(b,sizeof b,"PMU %s  radio %s  SD %s",pmuOk?"ok":"--",radioOk?"ok":"ERR",lg.card?"ok":"--");txtC(64,48,b,F_TINY);
 snprintf(b,sizeof b,"v%s  id !%08lx",FW_VERSION,(unsigned long)myId);txtC(64,60,b,F_TINY);}

void render(){
 u8g2.clearBuffer();u8g2.setDrawColor(1);u8g2.setFontMode(1);
 if(millis()<splashUntil)drawSplash();
 else{switch(screen){case S_HOME:drawHome();break;case S_MESH:drawMesh();break;case S_NOISE:drawNoise();break;case S_SCAN:drawScan();break;case S_GPS:drawGps();break;default:drawSet();}
  drawHeader();}
 u8g2.sendBuffer();}

// ================= NVS =================
void cfgLoad(){prefs.begin("tbeam",true);size_t n=prefs.getBytesLength("cfg");if(n==sizeof cfg)prefs.getBytes("cfg",&cfg,sizeof cfg);prefs.end();
 if(cfg.meshPreset>=pre::N)cfg.meshPreset=0;if(cfg.power>=pre::NPWR)cfg.power=2;cfg.autoPing%=4;cfg.contrast%=4;cfg.log&=1;cfg.gpsOn&=1;cfg.flip&=1;cfg.pingHops&=1;}
void cfgSave(){prefs.begin("tbeam",false);prefs.putBytes("cfg",&cfg,sizeof cfg);prefs.end();}
// Wlasny identyfikator wezla (losowy, trwaly) uzywany jako nadawca pingow Meshtastic.
void nodeIdLoad(){prefs.begin("tbeam",true);uint32_t v=0;if(prefs.getBytesLength("nid")==4)prefs.getBytes("nid",&v,4);prefs.end();
 if(!v||v==0xFFFFFFFFu||(v&0xFF)==0||(v&0xFF)==0xFF){do{v=esp_random();}while(!v||v==0xFFFFFFFFu||(v&0xFF)==0||(v&0xFF)==0xFF);prefs.begin("tbeam",false);prefs.putBytes("nid",&v,4);prefs.end();}
 myId=v;Serial.printf("# node id !%08lx\n",(unsigned long)myId);}
void displayApply(){static const uint8_t C[4]={8,48,128,255};u8g2.setContrast(C[cfg.contrast%4]);u8g2.setDisplayRotation(cfg.flip?U8G2_R2:U8G2_R0);}
void applyChange(unsigned item){
 cfgSave();
 if(item==0){mtab.clear();pingSt=Ping();meshSelId=0;lastRx=LastRx();nzMesh.reset();mtab.defHash=mh::chanHash(pre::get(cfg.meshPreset).name,mh::defaultKey(),16);if(mode==M_MESH)radioApply(profFor(M_MESH));}
 if(item==1&&mode==M_MESH)radioApply(profFor(M_MESH));
 if(item==2)lastAutoPingAt=millis();
 if(item==4&&cfg.log&&!lg.card)logOpen(false);
 if(item==5)gpsPower(cfg.gpsOn);
 if(item==6||item==7)displayApply();}
void changeSetting(unsigned i){
 switch(i){
  case 0:cfg.meshPreset=(cfg.meshPreset+1)%pre::N;break;
  case 1:cfg.power=(cfg.power+1)%pre::NPWR;break;
  case 2:cfg.autoPing=(cfg.autoPing+1)%4;break;
  case 3:cfg.pingHops^=1;break;
  case 4:cfg.log^=1;break;
  case 5:cfg.gpsOn^=1;break;
  case 6:cfg.contrast=(cfg.contrast+1)%4;break;
  default:cfg.flip^=1;}
 applyChange(i);}

void gotoScreen(int s){screen=(s+S_COUNT)%S_COUNT;Mode m=screen==S_SCAN?M_SCAN:M_MESH;if(m!=mode)switchMode(m);if(screen==S_SCAN)scanReset();dirty=true;}

// ================= Przyciski =================
// BOOT: zwykly GPIO. PWR: zdarzenia z AXP2101 (krotkie nacisniecie / przytrzymanie 1 s).
struct Btn{int pin;bool down=false,lng=false;uint32_t t=0;explicit Btn(int p):pin(p){}};
Btn btnBoot(PIN_BTN_BOOT);
int btnPoll(Btn& b,uint32_t now){   // 0 nic, 1 krotkie (po puszczeniu), 2 dlugie (po 700 ms trzymania)
 bool d=digitalRead(b.pin)==LOW;
 if(d&&!b.down){b.down=true;b.lng=false;b.t=now;return 0;}
 if(d&&b.down&&!b.lng&&now-b.t>=700){b.lng=true;return 2;}
 if(!d&&b.down){b.down=false;if(!b.lng&&now-b.t>=30)return 1;}
 return 0;}
uint32_t lastPmuPoll=0;
int pwrPoll(uint32_t now){
 if(!pmuOk)return 0;if(digitalRead(PIN_PMU_IRQ)!=LOW&&now-lastPmuPoll<250)return 0;lastPmuPoll=now;
 pmu.getIrqStatus();int r=pmu.isPekeyLongPressIrq()?2:pmu.isPekeyShortPressIrq()?1:0;pmu.clearIrqStatus();return r;}
void onButtons(uint32_t now){
 int a=btnPoll(btnBoot,now),k=pwrPoll(now);
 if((a||k)&&now<splashUntil){splashUntil=0;dirty=true;return;}   // pierwsze nacisniecie zamyka ekran powitalny
 if(a){dirty=true;
  if(screen==S_SET){if(a==1)setSel=(setSel+1)%SET_N;else gotoScreen(S_HOME);}
  else gotoScreen(screen+(a==1?1:-1));}
 if(k){dirty=true;
  if(screen==S_SET){if(k==1)changeSetting(setSel);else setSel=(setSel+SET_N-1)%SET_N;return;}
  if(k==1){switch(screen){
    case S_HOME:pingSend(now);break;
    case S_MESH:{unsigned idx[mh::Table::MAXN];unsigned n=mtab.order(now,idx);if(n){unsigned cur=0;for(unsigned i=0;i<n;i++)if(mtab.n[idx[i]].id==meshSelId)cur=i;meshSelId=mtab.n[idx[(cur+1)%n]].id;}}break;
    case S_NOISE:noiseWin=(noiseWin+1)%3;break;
    case S_SCAN:scanPaused=!scanPaused;break;
    case S_GPS:cfg.log^=1;applyChange(4);break;}}
  else{switch(screen){
    case S_HOME:mtab.clear();lastRx=LastRx();meshSelId=0;pingSt=Ping();break;
    case S_MESH:pingSend(now);break;
    case S_NOISE:nzMesh.reset();break;
    case S_SCAN:scanReset();break;
    case S_GPS:if(cfg.log)logOpen(true);break;}}}}

// ================= setup / loop =================
void setup(){
 Serial.begin(115200);delay(200);
 pinMode(PIN_BTN_BOOT,INPUT_PULLUP);pinMode(PIN_PMU_IRQ,INPUT_PULLUP);pinMode(PIN_LORA_DIO1,INPUT);
 pinMode(PIN_IMU_CS,OUTPUT);digitalWrite(PIN_IMU_CS,HIGH);pinMode(PIN_SD_CS,OUTPUT);digitalWrite(PIN_SD_CS,HIGH);pinMode(PIN_GPS_WAKE,OUTPUT);
 cfgLoad();
 pmuOk=pmuInit();delay(50);                      // zasilanie OLED, radia i GPS musi byc wlaczone przed ich inicjalizacja
 u8g2.setBusClock(400000);u8g2.begin();displayApply();
 Serial1.begin(GPS_BAUD,SERIAL_8N1,PIN_GPS_RX,PIN_GPS_TX);gpsPower(cfg.gpsOn);
 sdSpi.begin(PIN_SD_SCK,PIN_SD_MISO,PIN_SD_MOSI,PIN_SD_CS);
 mtab.defHash=mh::chanHash(pre::get(cfg.meshPreset).name,mh::defaultKey(),16);
 nodeIdLoad();radioInit();batRead();
 Serial.println(LOG_HEADER);
 if(cfg.log)logOpen(false);
 splashUntil=millis()+2500;render();}

void loop(){
 uint32_t now=millis();
 onButtons(now);gpsPoll();txPoll(now);rxPoll(now);pingTick(now);noiseTick(now);
 static uint32_t lastScan=0;if(mode==M_SCAN&&now-lastScan>=40){lastScan=now;scanTick();}
 if(now-lastBatAt>=10000||!lastBatAt){lastBatAt=now?now:1;batRead();dirty=true;}
 if((dirty&&now-lastDraw>=100)||now-lastDraw>=1000){lastDraw=now;dirty=false;render();}
 delay(1);}
