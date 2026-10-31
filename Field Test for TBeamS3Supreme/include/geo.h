#pragma once
#include <stdint.h>
#include <stdio.h>
#include <math.h>
// Odleglosc i kierunek miedzy dwoma punktami (stopnie, WGS84 jako kula R = 6371 km) oraz formatowanie do ekranu i logu.
namespace geo {
constexpr double R=6371000.0,D2R=M_PI/180.0;
inline double dist(double la1,double lo1,double la2,double lo2){double p1=la1*D2R,p2=la2*D2R,dp=(la2-la1)*D2R,dl=(lo2-lo1)*D2R;
 double a=sin(dp/2)*sin(dp/2)+cos(p1)*cos(p2)*sin(dl/2)*sin(dl/2);return 2*R*atan2(sqrt(a),sqrt(1-a));}
inline int bearing(double la1,double lo1,double la2,double lo2){double p1=la1*D2R,p2=la2*D2R,dl=(lo2-lo1)*D2R;
 double b=atan2(sin(dl)*cos(p2),cos(p1)*sin(p2)-sin(p1)*cos(p2)*cos(dl))/D2R;int r=int(lround(b));return (r%360+360)%360;}
// "850m", "2.3km", "17km"
inline const char* fmtDist(double m,char* b,size_t n){if(m<1000)snprintf(b,n,"%dm",int(lround(m)));else if(m<10000)snprintf(b,n,"%.1fkm",m/1000);else snprintf(b,n,"%dkm",int(lround(m/1000)));return b;}
inline const char* compass(int deg){static const char* c[8]={"N","NE","E","SE","S","SW","W","NW"};return c[((deg+22)%360)/45];}
}
