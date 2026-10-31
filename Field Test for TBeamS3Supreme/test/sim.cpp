// Symulacja calego firmware na hoscie: wirtualne radio SX1262, AXP2101, GPS (NMEA), karta SD, przyciski, zegar,
// ekran OLED rysowany prawdziwa biblioteka u8g2 (zrzuty ekranow do shots/*.pbm, potem tools/render.py -> PNG).
#include <cassert>
#include <map>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>
#include "../stubs/Arduino.h"
#include "../stubs/RadioLib.h"
#include "../stubs/Preferences.h"
#include "../stubs/SD.h"
#include "../stubs/XPowersLib.h"
#include "../stubs/Wire.h"
SimRadio gSim;SimSd gSd;SimPmu gPmu;SDFS SD;TwoWire Wire,Wire1;HardwareSerial Serial1;
static unsigned long gNow=0;static uint32_t gRand=1;static bool gBtn[64];static int gPinOut[64];static std::map<std::string,std::vector<uint8_t>> gNvs;
static std::vector<std::string> gSerial;
unsigned long millis(){return gNow;}void delay(unsigned long){}void delayMicroseconds(unsigned){}uint32_t esp_random(){return gRand++*2654435761u;}
void pinMode(int,int){}void digitalWrite(int p,int v){gPinOut[p]=v;}
int digitalRead(int p){if(p==1)return gSim.irq?1:0;if(p==40)return gPmu.keys.empty()?1:0;return gBtn[p]?0:1;}
size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t k=l<n-1?l:n-1;memcpy(d,s,k);d[k]=0;}return l;}
void SerialC::begin(unsigned){}int SerialC::printf(const char*,...){return 0;}void SerialC::println(const char* s){gSerial.push_back(s);}SerialC Serial;
static std::string gNs;
void Preferences::begin(const char* n,bool){gNs=n;}void Preferences::end(){}
size_t Preferences::getBytesLength(const char* k){auto i=gNvs.find(gNs+"/"+k);return i==gNvs.end()?0:i->second.size();}
size_t Preferences::getBytes(const char* k,void* b,size_t n){auto i=gNvs.find(gNs+"/"+k);if(i==gNvs.end())return 0;size_t m=std::min(n,i->second.size());memcpy(b,i->second.data(),m);return m;}
size_t Preferences::putBytes(const char* k,const void* b,size_t n){gNvs[gNs+"/"+k].assign((const uint8_t*)b,(const uint8_t*)b+n);return n;}
#include "../src/main.cpp"

static void run(unsigned ms,void(*hook)()=nullptr){for(unsigned i=0;i<ms;i++){++gNow;if(hook)hook();loop();}}
static void boot(bool lng=false){gBtn[0]=true;run(lng?760:80);gBtn[0]=false;run(60);}
static void pwr(bool lng=false){gPmu.keys.push_back(lng?2:1);run(300);}
// ---- zrzuty ekranu i kontrola ukladu ----
static int gOverlaps=0,gOob=0;
static void dump(const char* name){FILE* f=fopen((std::string("shots/")+name+".pbm").c_str(),"w");fprintf(f,"P1\n128 64\n");
 for(int y=0;y<64;y++){for(int x=0;x<128;x++)fputc(u8g2.frame[size_t(y)*128+x]?'1':'0',f);fputc('\n',f);}fclose(f);}
static void shot(const char* name){render();dump(name);auto& T=u8g2.texts;
 for(size_t i=0;i<T.size();i++){auto& a=T[i];if(a.x<0||a.y<0||a.x+a.w>128||a.y+a.h>64){++gOob;fprintf(stderr,"  [%s] text outside screen '%s' (%d,%d %dx%d)\n",name,a.s.c_str(),a.x,a.y,a.w,a.h);}
  for(size_t j=i+1;j<T.size();j++){auto& b=T[j];if(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h){++gOverlaps;fprintf(stderr,"  [%s] OVERLAP '%s'(%d,%d %dx%d) x '%s'(%d,%d %dx%d)\n",name,a.s.c_str(),a.x,a.y,a.w,a.h,b.s.c_str(),b.x,b.y,b.w,b.h);}}}}
// ---- NMEA z poprawna suma kontrolna ----
static std::string nmea(const std::string& body){uint8_t c=0;for(char ch:body)c^=uint8_t(ch);char t[8];snprintf(t,sizeof t,"*%02X\r\n",c);return "$"+body+t;}
static void gpsFix(bool ok){std::string gga=ok?"GPGGA,140211.00,5214.0700,N,02100.7200,E,1,08,1.2,112.0,M,34.0,M,,":"GPGGA,140211.00,,,,,0,03,,,M,,M,,";
 std::string rmc=ok?"GPRMC,140211.00,A,5214.0700,N,02100.7200,E,0.1,0.0,301026,,,A":"GPRMC,140211.00,V,,,,,,,301026,,,N";Serial1.feed(nmea(gga).c_str());Serial1.feed(nmea(rmc).c_str());}
static void gpsHook(){if(gNow%1000==0)gpsFix(true);}
// ---- pakiety Meshtastic ----
static std::vector<uint8_t> enc(uint32_t from,uint32_t to,uint32_t id,uint8_t flags,uint8_t chan,const std::vector<uint8_t>& plain){std::vector<uint8_t> p(16,0);
 for(int i=0;i<4;i++){p[i]=uint8_t(to>>(8*i));p[4+i]=uint8_t(from>>(8*i));p[8+i]=uint8_t(id>>(8*i));}p[12]=flags;p[13]=chan;std::vector<uint8_t> c(plain.size());mh::ctrCrypt(id,from,plain.data(),c.data(),plain.size());p.insert(p.end(),c.begin(),c.end());return p;}
static std::vector<uint8_t> nodeinfo(uint32_t from,uint32_t id,const char* lname,uint8_t hopLimit,uint8_t hopStart){std::vector<uint8_t> u;u.push_back(0x12);u.push_back(uint8_t(strlen(lname)));u.insert(u.end(),lname,lname+strlen(lname));u.push_back(0x1a);u.push_back(2);u.push_back('N');u.push_back('D');
 std::vector<uint8_t> pl={0x08,0x04,0x12,uint8_t(u.size())};pl.insert(pl.end(),u.begin(),u.end());return enc(from,0xffffffff,id,uint8_t(hopLimit|(hopStart<<5)),8,pl);}
static std::vector<uint8_t> position(uint32_t from,uint32_t id,double lat,double lon){int32_t la=int32_t(lround(lat*1e7)),lo=int32_t(lround(lon*1e7));std::vector<uint8_t> q={0x0d};for(int i=0;i<4;i++)q.push_back(uint8_t(uint32_t(la)>>(8*i)));q.push_back(0x15);for(int i=0;i<4;i++)q.push_back(uint8_t(uint32_t(lo)>>(8*i)));q.push_back(0x18);q.push_back(100);
 std::vector<uint8_t> pl={0x08,0x03,0x12,uint8_t(q.size())};pl.insert(pl.end(),q.begin(),q.end());return enc(from,0xffffffff,id,uint8_t(3|(3<<5)),8,pl);}
static std::vector<uint8_t> ack(uint32_t from,uint32_t pid,uint32_t req,int err,uint8_t flags=0x63){std::vector<uint8_t> d={0x08,0x05};if(err>=0){d.push_back(0x12);d.push_back(2);d.push_back(0x18);d.push_back(uint8_t(err));}d.push_back(0x35);for(int i=0;i<4;i++)d.push_back(uint8_t(req>>(8*i)));return enc(from,myId,pid,flags,8,d);}
static void rx(const std::vector<uint8_t>& p,float rssi,float snr,int err=0){gSim.rx.push_back(SimPkt{p,rssi,snr,err});gSim.arm();run(150);}
static int cols(const std::string& s){int n=1;for(char c:s)if(c==',')++n;return n;}
static std::vector<std::string> lines(const std::string& s){std::vector<std::string> o;size_t a=0;while(a<s.size()){size_t b=s.find('\n',a);if(b==std::string::npos)b=s.size();o.push_back(s.substr(a,b-a));a=b+1;}return o;}
static int countEv(const std::string& file,const char* ev){int n=0;for(auto& l:lines(gSd.files[file]))if(l.find(std::string(",")+ev+",")!=std::string::npos)++n;return n;}

int main(){
 system("mkdir -p shots");
 setup();
 // --- start: zasilanie, radio, log
 assert(pmuOk&&gPmu.on[XPOWERS_ALDO1]&&gPmu.on[XPOWERS_ALDO2]&&gPmu.on[XPOWERS_ALDO3]&&gPmu.on[XPOWERS_ALDO4]&&gPmu.on[XPOWERS_BLDO1]&&gPmu.offTime==XPOWERS_POWEROFF_6S);
 assert(gPmu.irqEn==(XPOWERS_AXP2101_PKEY_SHORT_IRQ|XPOWERS_AXP2101_PKEY_LONG_IRQ));
 assert(Wire1.sda==42&&Wire1.scl==41&&Serial1.baud==9600&&Serial1.rx==9&&Serial1.tx==8&&gPinOut[PIN_GPS_WAKE]==HIGH&&gPinOut[PIN_IMU_CS]==HIGH);
 assert(radioOk&&fabsf(gSim.freq-869.525f)<1e-3&&gSim.bw==250&&gSim.sf==11&&gSim.cr==5&&gSim.sync==0x2B&&gSim.pre==16&&gSim.pwr==14);   // LongFast
 assert(lg.card&&strcmp(lg.file,"/ft0001.csv")==0&&gSd.files["/ft0001.csv"]==std::string(LOG_HEADER)+"\n");
 assert(u8g2.contrast==128&&u8g2.bus==400000);
 shot("00_splash");run(2600);assert(millis()>splashUntil);
 shot("01_home_empty");
 // --- GPS: brak fixu, potem fix
 gotoScreen(S_GPS);shot("02_gps_nodata");gpsFix(false);run(50);assert(!fix.valid&&fix.sats==3);shot("03_gps_search");
 gpsFix(true);run(50);assert(fix.valid&&fabs(fix.lat-52.2345)<1e-4&&fabs(fix.lon-21.012)<1e-4&&fix.sats==8&&strcmp(fix.utc,"2026-10-30T14:02:11Z")==0);
 shot("04_gps_fix");
 // --- MESH: 10 wezlow z nazwami, 3 z pozycja, pakiet obcego kanalu, blad CRC
 gotoScreen(S_MESH);shot("05_mesh_empty");
 const char* names[10]={"Warszawa Mokotow","SP5JAN mobile","Pruszkow RPT","Ząbki dom","Legionowo","Piaseczno Bike","Otwock","Grodzisk","Kampinos Tower","Wawer"};
 for(int i=0;i<10;i++)rx(nodeinfo(0x1000+i,500+i,names[i],uint8_t(3-(i%4)),3),-75.f-i*4,8.f-i*1.6f);
 rx(position(0x1000,700,52.2000,21.0200),-76,7.5f);rx(position(0x1002,701,52.1650,20.8020),-84,2);rx(position(0x1008,702,52.3200,20.6500),-110,-12);
 {std::vector<uint8_t> p=enc(0x2222,0xffffffff,900,0x63,77,{0x08,0x01,0x12,0x01,'x'});rx(p,-118,-14);}
 rx(std::vector<uint8_t>(40,0x55),-100,0,RADIOLIB_ERR_CRC_MISMATCH);
 fprintf(stderr,"mesh: pkts=%lu nodes=%u crc=%lu\n",(unsigned long)mtab.packets,mtab.used,(unsigned long)crcErrors);assert(mtab.used==11&&crcErrors==1&&mtab.packets==14);
 {const mh::Node* n=mtab.find(0x1000);assert(n&&n->hasPos&&strcmp(n->name,"Warszawa Mokotow")==0);double d=nodeDist(*n);fprintf(stderr,"dist to 0x1000: %.0f m\n",d);assert(d>3700&&d<4000);
  const mh::Node* z=mtab.find(0x1003);assert(z&&strcmp(z->name,"Zabki dom")==0);assert(!mtab.find(0x2222)->name[0]);}
 shot("06_mesh_list");pwr();pwr();shot("07_mesh_scrolled");
 // --- ping recznie (przytrzymanie PWR) i ACK
 {unsigned idx[40];mtab.order(gNow,idx);meshSelId=0x1002;size_t t0=gSim.tx.size();shot("08_mesh_sel");
  pwr(true);assert(gSim.tx.size()==t0+1&&pingSt.st==PG_WAIT&&pingSt.target==0x1002);
  {auto& pk=gSim.tx.back();mh::Hdr h;assert(mh::parse(pk.data(),pk.size(),h)&&h.to==0x1002&&h.from==myId&&h.wantAck&&h.hopLimit==0&&h.chan==8);}
  shot("09_ping_wait");run(1500);rx(ack(0x1002,0x5001,pingSt.id+1,0),-90,4);assert(pingSt.st==PG_WAIT);   // obca odpowiedz ignorowana
  rx(ack(0x1002,0x5003,pingSt.id,0),-97,3.25f);assert(pingSt.st==PG_ACK&&pingSt.acks==1&&pingSt.rssi==-97&&pingSt.hops==0);shot("10_ping_ack");
  pwr(true);run(13000);assert(pingSt.st==PG_TIMEOUT);shot("11_ping_timeout");
  pwr(true);rx(ack(0x1002,0x5004,pingSt.id,5),-100,-3);assert(pingSt.st==PG_NAK&&pingSt.err==5);shot("12_ping_nak");
  duty.add(gNow,3600000);pwr(true);assert(pingSt.st==PG_BLOCKED);shot("13_ping_blocked");duty=pre::Duty();}
 // --- ping przez przekazniki (ustawienie Ping hops)
 cfg.pingHops=1;pwr(true);{mh::Hdr h;assert(mh::parse(gSim.tx.back().data(),gSim.tx.back().size(),h)&&h.hopLimit==3&&h.hopStart==3);}
 rx(ack(0x1002,0x5005,pingSt.id,0,uint8_t(1|(3<<5))),-95,1);assert(pingSt.st==PG_ACK&&pingSt.hops==2);cfg.pingHops=0;
 // --- auto ping co 30 s
 {gotoScreen(S_SET);setSel=2;pwr();assert(cfg.autoPing==1);gotoScreen(S_HOME);size_t t0=gSim.tx.size();run(95000,gpsHook);size_t n=gSim.tx.size()-t0;fprintf(stderr,"auto ping: %zu in 95 s\n",n);assert(n>=3&&n<=4);
  shot("14_home_autoping");gotoScreen(S_SET);setSel=2;for(int i=0;i<3;i++)pwr();assert(cfg.autoPing==0);gotoScreen(S_HOME);}
 // --- HOME z danymi, ostatni pakiet z jakoscia
 rx(nodeinfo(0x1001,555,"SP5JAN mobile",2,3),-112,-9.5f);shot("15_home");
 // --- NOISE: podloga -112, impuls -60 raz na 30 s
 gotoScreen(S_NOISE);nzMesh.reset();
 run(700000,[](){static unsigned c=0;++c;gSim.noise=(c%30000<400)?-60.f:-112.f+float((c*7)%5)-2;if(gNow%1000==0)gpsFix(true);});
 {nz::Stats s=nzMesh.stats(600);fprintf(stderr,"noise10m: now=%d floor=%d peak=%d busy=%.1f\n",s.now,s.floor,s.peak,s.busy);assert(s.has&&s.floor<=-112&&s.floor>=-116&&s.peak==-60);}
 shot("16_noise_1m");pwr();shot("17_noise_10m");pwr();shot("18_noise_1h");pwr();pwr(true);shot("19_noise_empty");
 // --- SCAN
 gotoScreen(S_SCAN);assert(mode==M_SCAN);auto scanHook=[](){gSim.noise=-112;if(scanIdx==26||scanIdx==27)gSim.noise=-72;if(scanIdx==65)gSim.noise=-95;};run(4000,scanHook);shot("20_scan");
 {size_t t0=gSim.tx.size();pingSend(gNow);assert(gSim.tx.size()==t0);}   // w trybie skanu nie nadajemy
 pwr();assert(scanPaused);shot("21_scan_paused");pwr();pwr(true);run(8000,scanHook);
 int pk=0;for(unsigned i=0;i<SCAN_N;i++)if(scanPeak[i]>-100)++pk;assert(pk>=2);
 gotoScreen(S_MESH);assert(mode==M_MESH&&fabsf(gSim.freq-869.525f)<1e-3&&gSim.sync==0x2B);
 // --- log CSV: kazdy wiersz ma tyle kolumn co naglowek
 {auto L=lines(gSd.files["/ft0001.csv"]);int hc=cols(LOG_HEADER);for(size_t i=0;i<L.size();i++)if(cols(L[i])!=hc){fprintf(stderr,"bad row %zu: %s\n",i,L[i].c_str());assert(0);}
  int rxN=countEv("/ft0001.csv","RX"),ack=countEv("/ft0001.csv","PING_ACK"),to=countEv("/ft0001.csv","PING_TIMEOUT"),nak=countEv("/ft0001.csv","PING_NAK"),bl=countEv("/ft0001.csv","PING_BLOCKED"),nz=countEv("/ft0001.csv","NOISE");
  fprintf(stderr,"log rows=%zu RX=%d ACK=%d NAK=%d TIMEOUT=%d BLOCKED=%d NOISE=%d\n",L.size(),rxN,ack,nak,to,bl,nz);
  assert(rxN>=15&&ack==2&&nak==1&&to>=4&&bl==1&&nz>=60);
  for(auto& l:L)if(l.find(",PING_ACK,")!=std::string::npos){fprintf(stderr,"  %s\n",l.c_str());assert(l.find("52.23")!=std::string::npos&&l.find("!00001002")!=std::string::npos&&l.find("Pruszkow RPT")!=std::string::npos);break;}
  for(auto& l:L)if(l.find(",RX,!00001000")!=std::string::npos&&l.find("52.2345")!=std::string::npos){fprintf(stderr,"  %s\n",l.c_str());break;}
  assert(gSerial.size()>=L.size());}
 // --- GPS: nowy plik logu (PWR przytrzymany), wylaczenie logu (PWR krotko)
 gotoScreen(S_GPS);gpsFix(true);run(50);shot("22_gps_log");pwr(true);assert(strcmp(lg.file,"/ft0002.csv")==0&&gSd.files.count("/ft0002.csv"));
 pwr();assert(!cfg.log);shot("23_gps_logoff");size_t r0=gSd.files["/ft0002.csv"].size();run(20000);assert(gSd.files["/ft0002.csv"].size()==r0);pwr();assert(cfg.log);
 // --- GPS wylaczony w SETUP
 gotoScreen(S_SET);setSel=5;pwr();assert(!cfg.gpsOn&&!gPmu.on[XPOWERS_ALDO4]&&gPinOut[PIN_GPS_WAKE]==LOW&&!fix.valid);gotoScreen(S_GPS);shot("24_gps_off");gotoScreen(S_HOME);shot("25_home_gpsoff");
 gotoScreen(S_SET);setSel=5;pwr();assert(cfg.gpsOn&&gPmu.on[XPOWERS_ALDO4]);
 // --- ustawienia: wszystkie pozycje, NVS
 gotoScreen(S_SET);for(unsigned i=0;i<SET_N;i++){setSel=i;shot(("3"+std::to_string(i)+"_set").c_str());}
 for(unsigned i=0;i<SET_N;i++)for(int k=0;k<8;k++)changeSetting(i);
 cfg=Cfg();applyChange(0);setSel=0;pwr();assert(cfg.meshPreset==1&&mtab.used==0&&gSim.sf==12&&gSim.bw==125);   // LongSlow
 cfg.meshPreset=0;applyChange(0);
 cfg.power=4;cfg.autoPing=3;cfg.contrast=0;cfg.flip=1;cfgSave();Cfg saved=cfg;cfg=Cfg();cfgLoad();assert(memcmp(&saved,&cfg,sizeof cfg)==0);
 applyChange(6);assert(u8g2.contrast==8&&u8g2.rot==U8G2_R2);cfg=Cfg();cfgSave();applyChange(7);applyChange(0);
 // --- przyciski: obieg ekranow (BOOT krotko = dalej, dlugo = wstecz)
 gotoScreen(S_HOME);for(int i=0;i<S_COUNT-1;i++){boot();shot(("4"+std::to_string(i)+"_cycle").c_str());}assert(screen==S_SET);boot();assert(screen==S_SET&&setSel==1);boot(true);assert(screen==S_HOME);boot(true);assert(screen==S_SET);boot(true);assert(screen==S_HOME);
 // --- bateria, ladowanie, brak baterii
 gPmu.chg=true;gPmu.vbus=true;gPmu.pct=45;run(10100);shot("50_home_charging");gPmu.batt=false;run(10100);assert(batPct<0);shot("51_home_nobatt");gPmu.batt=true;gPmu.chg=false;
 // --- karta: blad zapisu wylacza karte, log idzie dalej na port szeregowy
 gSd.failWrites=true;size_t s0=gSerial.size();run(40000);assert(!lg.card&&gSerial.size()>s0);gotoScreen(S_GPS);shot("52_gps_sdfail");gSd.failWrites=false;
 // --- bledy radia
 gSim.beginOk=false;radioOk=false;gotoScreen(S_HOME);shot("53_home_radio_err");
 fprintf(stderr,"overlaps=%d textOob=%d\n",gOverlaps,gOob);
 assert(gOverlaps==0&&gOob==0);
 puts("sim OK");return 0;}
