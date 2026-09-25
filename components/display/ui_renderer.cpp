#include "ui_renderer.h"
#include "font_data.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace {
constexpr uint16_t rgb(unsigned r,unsigned g,unsigned b) { return ((r>>3)<<11)|((g>>2)<<5)|(b>>3); }
constexpr auto BG=rgb(13,23,29), PANEL=rgb(23,36,43), INK=rgb(245,241,222), MUTED=rgb(146,166,174),
    GOLD=rgb(248,203,103), LINE=rgb(43,61,69), GREEN=rgb(133,208,176);
class Canvas {
public:
    Canvas(uint16_t *p,int offset,int rows,DisplayTheme style):pixels(p),top(offset),bottom(offset+rows),theme(style){}
    uint16_t color(uint16_t value) const {
        if (theme==DisplayTheme::Classic) return value;
        if (theme==DisplayTheme::Amber) {
            if (value==BG) return rgb(15,11,5);
            if (value==PANEL) return rgb(35,24,9);
            if (value==INK) return rgb(255,219,149);
            if (value==MUTED) return rgb(191,143,70);
            if (value==GOLD || value==GREEN) return rgb(255,184,61);
            if (value==LINE) return rgb(88,59,23);
        } else {
            if (value==BG) return rgb(202,220,159);
            if (value==PANEL || value==LINE) return rgb(163,188,116);
            if (value==INK || value==GOLD || value==GREEN) return rgb(38,58,32);
            if (value==MUTED) return rgb(81,109,54);
        }
        // Tint the existing coin, whole burger and sleeping cat with the theme.
        const unsigned light=(((value>>11)&31)*77/31+((value>>5)&63)*150/63+(value&31)*29/31);
        if (theme==DisplayTheme::Amber) {
            return light<50?rgb(35,24,9):light<120?rgb(137,86,28):light<190?rgb(226,147,45):rgb(255,219,149);
        }
        return light<60?rgb(38,58,32):light<130?rgb(81,109,54):light<205?rgb(163,188,116):rgb(202,220,159);
    }
    void clip(int x=0,int y=0,int w=SCREEN_WIDTH,int h=SCREEN_HEIGHT) { left=x;right=x+w;clip_top=y;clip_bottom=y+h; }
    void rect(int x,int y,int w,int h,uint16_t color,uint8_t opacity=15) {
        const int x0=std::max(left,x), x1=std::min(right,x+w);
        const int y0=std::max(std::max(top,clip_top),y), y1=std::min(std::min(bottom,clip_bottom),y+h);
        if (x0>=x1 || y0>=y1) return;
        if (opacity==15) {
            color=this->color(color);
            for (int row=y0;row<y1;++row) std::fill(pixels+(row-top)*SCREEN_WIDTH+x0,pixels+(row-top)*SCREEN_WIDTH+x1,color);
        } else for (int row=y0;row<y1;++row) for (int col=x0;col<x1;++col) pixel(col,row,color,opacity);
    }
    void ellipse(float x,float y,float rx,float ry,uint16_t color) {
        if (rx<.5f || ry<.5f) return;
        const int first=std::max(top,int(std::floor(y-ry))), last=std::min(bottom-1,int(std::ceil(y+ry)));
        for (int row=first;row<=last;++row) {
            const float dy=(row-y)/ry;
            if (std::abs(dy)>1) continue;
            const float span=rx*std::sqrt(std::max(0.f,1-dy*dy));
            const int start=int(std::ceil(x-span)),end=int(std::floor(x+span));
            rect(start,row,end-start+1,1,color);
        }
    }
    void pixel(int x,int y,uint16_t color,uint8_t alpha=15) {
        if (theme==DisplayTheme::Handheld) alpha=alpha>=8?15:0;
        color=this->color(color);
        if (x<left || x>=right || y<std::max(top,clip_top) || y>=std::min(bottom,clip_bottom) || !alpha) return;
        auto &dst=pixels[(y-top)*SCREEN_WIDTH+x];
        if (alpha==15) { dst=color; return; }
        const unsigned a=alpha,b=15-alpha;
        dst=uint16_t((((((color>>11)&31)*a+((dst>>11)&31)*b)/15)<<11) |
            (((((color>>5)&63)*a+((dst>>5)&63)*b)/15)<<5) |
            (((color&31)*a+(dst&31)*b)/15));
    }
    static uint32_t next(const char *&s) {
        const auto a=static_cast<uint8_t>(*s++);
        if (a<128) return a;
        int n=(a&0xe0)==0xc0 ? 1 : (a&0xf0)==0xe0 ? 2 : (a&0xf8)==0xf0 ? 3 : 0;
        if (!n) return '?';
        uint32_t code=a & (n==1?31:n==2?15:7);
        for (int i=0;i<n;++i) { if (!*s || (static_cast<uint8_t>(*s)&0xc0)!=0x80) return '?'; code=(code<<6)|(static_cast<uint8_t>(*s++)&63); }
        return code;
    }
    static const Glyph *glyph(uint32_t code,int size) {
        // Sorted by (font size, codepoint); no allocation or text layout cache needed.
        size_t lo=0,hi=GLYPH_COUNT;
        const uint32_t key=(uint32_t(size)<<21)|code;
        while (lo<hi) { const size_t mid=(lo+hi)/2; if (FONT_GLYPHS[mid].key<key) lo=mid+1; else hi=mid; }
        return lo<GLYPH_COUNT && FONT_GLYPHS[lo].key==key ? &FONT_GLYPHS[lo] : nullptr;
    }
    static int width(const char *s,int size) {
        int w=0;
        while (*s) { auto *g=glyph(next(s),size); if (!g) g=glyph('?',size); if (g) w+=g->advance; }
        return w;
    }
    void text(int x,int y,const char *s,int size,uint16_t color,int max_width=320,uint8_t opacity=15) {
        const int right=x+max_width;
        while (*s) {
            auto *g=glyph(next(s),size); if (!g) g=glyph('?',size); if (!g) continue;
            if (x+g->advance>right) break;
            if (y+g->top+g->height>top && y+g->top<bottom) for (int row=0;row<g->height;++row) for (int col=0;col<g->width;++col) {
                const unsigned i=row*g->width+col;
                const uint8_t packed=FONT_PIXELS[g->offset+i/2];
                const uint8_t coverage=i%2 ? packed&15 : packed>>4;
                pixel(x+g->left+col,y+g->top+row,color,(coverage*opacity+7)/15);
            }
            x+=g->advance;
        }
    }
    void center(int cx,int y,const char *s,int size,uint16_t color) { text(cx-width(s,size)/2,y,s,size,color); }
    void zoom_text(int cx,int cy,const char *s,int size,float scale,uint16_t color,uint8_t opacity) {
        const float origin_x=cx-width(s,size)*scale/2.f,origin_y=cy-size*scale/2.f;
        int advance=0;
        while (*s) {
            auto *g=glyph(next(s),size); if (!g) g=glyph('?',size); if (!g) continue;
            const int x=int(std::lround(origin_x+(advance+g->left)*scale));
            const int y=int(std::lround(origin_y+g->top*scale));
            const int w=std::max(1,int(std::lround(g->width*scale)));
            const int h=std::max(1,int(std::lround(g->height*scale)));
            // Render only this strip; scaling needs no temporary framebuffer.
            const int y0=std::max({0,top-y,clip_top-y}),y1=std::min({h,bottom-y,clip_bottom-y});
            for (int row=y0;row<y1;++row) for (int col=0;col<w;++col) {
                const unsigned i=(row*g->height/h)*g->width+col*g->width/w;
                const uint8_t packed=FONT_PIXELS[g->offset+i/2];
                const uint8_t coverage=i%2 ? packed&15 : packed>>4;
                pixel(x+col,y+row,color,(coverage*opacity+7)/15);
            }
            advance+=g->advance;
        }
    }
private:
    uint16_t *pixels; int top,bottom;
    int left=0,right=SCREEN_WIDTH,clip_top=0,clip_bottom=SCREEN_HEIGHT;
    DisplayTheme theme;
};
void money(Canvas &c,int x,int y,double amount,int max_width=182,uint16_t color=GOLD) {
    char value[32]; std::snprintf(value,sizeof(value),"%.2f",amount);
    int size=32;
    if (Canvas::width(value,size)>max_width) size=24;
    if (Canvas::width(value,size)>max_width) size=16;
    c.text(x,y,value,size,color,max_width);
}
void money_gain(Canvas &c,const UiModel &m) {
    if (m.gain_money<=0 || m.gain_progress>=1.f) return;
    const float t=std::clamp(m.gain_progress,0.f,1.f);
    // A quick upward hop, a short rebound, then a gentle rise while fading out.
    float lift;
    if (t<.28f) {
        const float remaining=1.f-t/.28f;
        lift=10.f*(1.f-remaining*remaining*remaining);
    } else if (t<.48f) {
        const float fall=(t-.28f)/.20f;
        lift=10.f-4.f*fall*fall;
    } else if (t<.64f) {
        lift=6.f+2.f*std::sin((t-.48f)/.16f*3.1415927f);
    } else lift=6.f+3.f*(t-.64f)/.36f;
    const auto opacity=uint8_t(std::lround(15.f*std::clamp((1.f-t)/.35f,0.f,1.f)));
    char value[32]; std::snprintf(value,sizeof(value),"+%.2f",m.gain_money);
    int size=16;
    if (Canvas::width(value,size)>140) size=12;
    if (Canvas::width(value,size)>140) size=10;
    // Above the total, beside NT$: keep clear of the title and coin scene even
    // when the salary total or this increment needs more digits.
    c.clip(50,51,144,29);
    c.text(190-Canvas::width(value,size),62-int(std::lround(lift)),value,size,GREEN,140,opacity);
    c.clip();
}
void duration(char *buffer,size_t capacity,uint32_t seconds) {
    std::snprintf(buffer,capacity,"%02lu:%02lu:%02lu",static_cast<unsigned long>(seconds/3600),
        static_cast<unsigned long>(seconds/60%60),static_cast<unsigned long>(seconds%60));
}
const char *work_label(WorkState state) {
    switch(state) {
        case WORK_STATE_DAY_OFF:return "放假啦~";
        case WORK_STATE_NO_CALENDAR:return "行事曆待更新";
        case WORK_STATE_BEFORE_WORK:return "還沒開偷";
        case WORK_STATE_WORKING_MORNING:case WORK_STATE_WORKING_AFTERNOON:return "正在偷薪水";
        case WORK_STATE_LUNCH:return "午休中，等等繼續偷";
        case WORK_STATE_AFTER_WORK:return "下班偷完了";
        default:return "等待時間同步";
    }
}
void coin(Canvas &c,const Coin &coin) {
    // Face-on at rest: visible circular rims meet where the physics colliders meet.
    const float spin=coin.sleeping ? 0.f : std::min(1.f,std::abs(coin.angularVelocity)/3.f);
    const float face=1.f-spin*(1.f-std::max(.35f,std::abs(std::cos(coin.rotation))));
    const float rx=coin.radius*face*(1+coin.squash*.65f),ry=coin.radius*(1-coin.squash);
    const float height=std::max(0.f,CoinPhysicsEngine::FLOOR-coin.y-coin.radius);
    if (height>2) c.ellipse(coin.x,CoinPhysicsEngine::FLOOR+2,std::max(3.f,coin.radius-height*.08f),1,rgb(7,13,17));
    c.ellipse(coin.x+1,coin.y+1,rx,ry,rgb(145,87,31));
    c.ellipse(coin.x,coin.y,rx,ry,rgb(251,211,117));
    c.ellipse(coin.x,coin.y,rx-1,ry-1,rgb(166,107,37));
    c.ellipse(coin.x,coin.y,rx-2,ry-2,rgb(231,167,58));
    c.ellipse(coin.x,coin.y-1,rx-3,ry-3,rgb(248,196,81));
    // Short moving highlight arc, with an inset rim and a compressed dollar mark.
    for (int i=0;i<14;++i) {
        const float angle=-2.7f+i*.07f+std::sin(coin.rotation)*.22f;
        c.pixel(int(coin.x+std::cos(angle)*(rx-1)),int(coin.y+std::sin(angle)*(ry-1)),INK);
    }
    if (face>.28f) {
        const int cx=int(coin.x), cy=int(coin.y);
        const int w=std::max(1,int(coin.radius*.25f*face)), h=std::max(3,int(coin.radius*.5f));
        const auto ink=rgb(147,88,26);
        c.rect(cx,cy-h-1,1,h*2+3,ink);
        c.rect(cx-w,cy-h,w*2+1,1,ink);c.rect(cx-w,cy-h,1,h,ink);
        c.rect(cx-w,cy,w*2+1,1,ink);c.rect(cx+w,cy,1,h,ink);
        c.rect(cx-w,cy+h,w*2+1,1,ink);
    }
}
void boot_scene(Canvas &c,const UiModel &m) {
    const float t=std::clamp(m.boot_progress,0.f,1.f),seconds=t*5.f;
    c.text(18,12,"STARTING UP",10,MUTED);
    char version[sizeof(m.firmware)+2];
    std::snprintf(version,sizeof(version),"v%s",m.firmware);
    c.text(302-Canvas::width(version,12),10,version,12,INK);

    c.clip(24,30,272,64);
    c.ellipse(160,62,49,26,PANEL);
    c.ellipse(160,62,45,23,BG);
    c.ellipse(160,88,23,2,LINE);
    for (int i=0;i<6;++i) {
        const float angle=seconds*1.8f+i*1.04719755f;
        const int x=int(std::lround(160+std::cos(angle)*47));
        const int y=int(std::lround(62+std::sin(angle)*24));
        c.rect(x-1,y-1,3,3,i%2?GREEN:GOLD);
    }
    const float enter=std::clamp(seconds/.85f,0.f,1.f),u=enter-1.f;
    const float drop=1.f+2.70158f*u*u*u+1.70158f*u*u;
    Coin icon{}; icon.x=160; icon.y=59-(1.f-drop)*78+std::sin(seconds*3.f)*2;
    icon.radius=21; icon.rotation=seconds*4.f; icon.angularVelocity=2.5f;
    coin(c,icon);
    c.clip();

    c.center(160,96,"薪水小偷計算器",24,GOLD);
    c.center(160,126,"SALARY THIEF CALCULATOR",10,MUTED);
    // This bar tracks the short intro, never claims network/download progress.
    c.rect(48,149,224,3,LINE);
    c.rect(48,149,int(std::lround(224*t)),3,GREEN);
}
void lunch_scene(Canvas &c,uint32_t milliseconds) {
    const uint32_t cycle=milliseconds%4800;
    const float phase=float(cycle)/4800.f;
    const int sway=int(std::lround(std::sin(phase*6.2831853f)*5.f));
    const int bob=int(std::lround(std::sin(phase*6.2831853f)*2.f));
    const auto crust=rgb(189,112,45), bun=rgb(244,185,91), sesame=rgb(255,226,167);
    const auto patty=rgb(111,61,41), tomato=rgb(224,96,74), lettuce=rgb(118,187,102);
    c.ellipse(259,132,43,4,rgb(7,13,17));
    c.ellipse(259,127,44,6,rgb(66,89,96));
    c.ellipse(259,125,40,4,rgb(182,199,196));
    c.ellipse(259,125,32,2,rgb(116,145,146));

    // Keep the burger whole and gently sway it above the stationary plate.
    c.ellipse(259+sway,110+bob,33,11,crust);
    c.ellipse(259+sway,108+bob,33,9,bun);
    c.rect(227+sway,99+bob,65,10,patty);
    c.ellipse(259+sway,100+bob,33,5,patty);
    c.rect(229+sway,94+bob,61,5,GOLD);
    for (int row=0;row<7;++row) c.rect(268+row+sway,98+bob+row,14-row*2,1,GOLD);
    c.rect(227+sway,89+bob,65,6,tomato);
    for (int x=231;x<291;x+=10) c.ellipse(float(x+sway),88.f+bob,7,4,lettuce);
    c.ellipse(259+sway,80+bob,34,18,crust);
    c.ellipse(259+sway,77+bob,33,17,bun);
    c.rect(227+sway,80+bob,65,7,bun);
    const int seeds[][2]={{241,70},{253,65},{269,68},{279,74},{235,77}};
    for (const auto &seed:seeds) c.ellipse(float(seed[0]+sway),float(seed[1]+bob),2,1,sesame);
    c.ellipse(247+sway,79.f+bob,2,2,patty);c.ellipse(260+sway,79.f+bob,2,2,patty);
    c.rect(252+sway,82+bob,4,1,patty);
}
void holiday_scene(Canvas &c,uint32_t milliseconds) {
    const float phase=float(milliseconds%4000)/4000.f;
    const int sway=int(std::lround(std::sin(phase*6.2831853f)*3.f));
    c.ellipse(289,52,9,9,GOLD);
    for (int i=0;i<8;++i) {
        const float angle=i*.7853982f+phase*.4f;
        c.rect(288+int(std::cos(angle)*14),51+int(std::sin(angle)*14),2,2,GOLD);
    }
    c.ellipse(226+sway,57,14,5,INK);c.ellipse(222+sway,53,7,7,INK);
    c.ellipse(230+sway,52,8,8,INK);
    const auto sand=rgb(224,184,111),sea=rgb(82,171,180),cloth=rgb(244,145,106);
    c.ellipse(258,130,48,8,sand);
    for (int x=210;x<311;x+=12) c.ellipse(float(x),137+std::sin(phase*6.2831853f+x*.12f)*2,8,2,sea);
    c.rect(263,85,3,43,MUTED);
    // A gently swaying parasol and deck chair make holidays distinct from sleep.
    for (int row=0;row<23;++row) {
        const int half=int(std::sqrt(std::max(0.f,1.f-std::pow((22-row)/23.f,2.f)))*32);
        c.rect(264+sway-half,70+row,half*2+1,1,row<9?INK:GOLD);
    }
    for (int i=0;i<19;++i) c.rect(226+i/2,99+i,5,2,i%6<3?cloth:INK);
    c.rect(234,117,24,4,cloth);
    for (int i=0;i<9;++i) {
        c.rect(235-i/2,120+i,2,1,MUTED);c.rect(254+i/2,120+i,2,1,MUTED);
    }
}
void rest_scene(Canvas &c,uint32_t milliseconds) {
    const float phase=float(milliseconds%4000)/4000.f;
    const float breath=.5f-.5f*std::cos(phase*6.2831853f);
    const auto fur=rgb(222,169,106), highlight=rgb(245,200,139), stripe=rgb(177,118,66);
    const auto cushion=rgb(69,116,132), cushion_light=rgb(95,149,158);
    // Crescent moon, cushion and a sleeping cat with a slow breathing motion.
    c.ellipse(227,52,10,10,GOLD);c.ellipse(231,48,9,9,BG);
    c.pixel(248,45,MUTED);c.pixel(297,67,MUTED);
    c.ellipse(261,135,44,4,rgb(7,13,17));
    c.ellipse(261,129,44,8,cushion);c.ellipse(261,126,42,6,cushion_light);
    c.ellipse(266,111-breath,32,18+breath,fur);
    c.ellipse(263,117,23,10,highlight);
    for (int x=262;x<=278;x+=8) c.ellipse(float(x),98-breath,2,4,stripe);
    c.ellipse(282,115,14,12,stripe);c.ellipse(282,113,13,11,fur);
    c.ellipse(283,113,7,6,stripe);c.ellipse(281,111,7,5,fur);
    const int head_y=106-int(std::lround(breath));
    for (int row=0;row<15;++row) {
        c.rect(225-row/3,head_y-24+row,2+row,1,fur);
        c.rect(248-row/2,head_y-24+row,2+row/2,1,fur);
    }
    for (int row=0;row<8;++row) {
        c.rect(226,head_y-19+row,1+row/2,1,rgb(201,130,103));
        c.rect(247-row/3,head_y-19+row,1+row/3,1,rgb(201,130,103));
    }
    c.ellipse(237,float(head_y),19,15,fur);
    c.ellipse(238,float(head_y+6),12,7,highlight);
    for (int x:{227,241}) {
        c.rect(x,head_y-1,2,2,stripe);c.rect(x+2,head_y+1,5,2,stripe);
        c.rect(x+7,head_y-1,2,2,stripe);
    }
    c.rect(237,head_y+6,3,2,stripe);c.rect(238,head_y+8,1,2,stripe);
    c.rect(219,head_y+5,7,1,stripe);c.rect(219,head_y+8,6,1,stripe);
    c.ellipse(240,123,8,4,highlight);c.ellipse(256,124,7,3,highlight);
    for (int i=0;i<3;++i) {
        const float drift=std::fmod(phase+float(i)/3.f,1.f);
        c.text(259+int(drift*31),78-int(drift*35),"Z",drift<.5f?10:12,drift>.8f?MUTED:INK);
    }
}
void header(Canvas &c,const UiModel &m) {
    if (m.theme==DisplayTheme::Amber) c.text(12,6,"> SALARY.LOG",12,GOLD);
    else if (m.theme==DisplayTheme::Handheld) c.text(12,6,"PAYDAY / GAME",12,INK);
    else c.text(12,6,"薪水小偷計算器",12,MUTED);
    const char *date=m.synced?m.date:"----/--/--";
    c.text(226-Canvas::width(date,12),6,date,12,MUTED);
    c.text(238,6,m.clock,12,INK);
    c.rect(12,26,296,m.theme==DisplayTheme::Handheld?2:1,LINE);
}
void footer(Canvas &c,const UiModel &m) {
    for (unsigned i=0;i<4;++i) {
        if (m.theme==DisplayTheme::Classic) c.ellipse(286+i*7,160,2,2,i==m.page ? GOLD : LINE);
        else c.rect(283+i*7,158,5,4,i==m.page?GOLD:LINE);
    }
}
void battery_info(Canvas &c,const BatteryStatus &battery) {
    char value[80];
    switch (battery.state) {
        case BatteryState::BatteryPower:
            std::snprintf(value,sizeof(value),"BAT:Likely  Supply %u.%02uV",unsigned(battery.supply_mv/1000),unsigned(battery.supply_mv%1000/10));break;
        case BatteryState::ExternalPower:
            std::snprintf(value,sizeof(value),"BAT:Unknown (USB/5V)  %u.%02uV",unsigned(battery.supply_mv/1000),unsigned(battery.supply_mv%1000/10));break;
        case BatteryState::Unknown:
            std::snprintf(value,sizeof(value),"BAT:Unknown  Supply %u.%02uV",unsigned(battery.supply_mv/1000),unsigned(battery.supply_mv%1000/10));break;
        case BatteryState::Unavailable:std::snprintf(value,sizeof(value),"BAT:Unknown / Read unavailable");break;
        default:std::snprintf(value,sizeof(value),"BAT:Checking...");break;
    }
    c.clip(12,132,296,14);
    c.text(12,134,value,10,battery.state==BatteryState::BatteryPower?GREEN:MUTED,296);
    c.clip();
}
void frame(Canvas &c,const UiModel &m) {
    if (m.theme==DisplayTheme::Amber) {
        // Open corners suggest a terminal viewport without covering any text.
        for (const int x:{3,310}) for (const int y:{2,165}) c.rect(x,y,7,1,GOLD);
        for (const int x:{3,316}) for (const int y:{2,159}) c.rect(x,y,1,7,GOLD);
    } else if (m.theme==DisplayTheme::Handheld) {
        c.rect(3,2,314,2,INK);c.rect(3,166,314,2,INK);
        c.rect(3,2,2,166,INK);c.rect(315,2,2,166,INK);
    }
}
void scene_frame(Canvas &c,const UiModel &m) {
    if (m.theme==DisplayTheme::Classic) return;
    if (m.theme==DisplayTheme::Amber) {
        for (int y=37;y<140;y+=6) c.rect(203,y,110,1,PANEL);
        c.rect(204,34,10,1,GOLD);c.rect(303,34,10,1,GOLD);
        c.rect(204,34,1,6,GOLD);c.rect(312,34,1,6,GOLD);
    } else {
        c.rect(203,33,111,2,MUTED);c.rect(203,139,111,2,MUTED);
        c.rect(203,33,2,108,MUTED);c.rect(312,33,2,108,MUTED);
    }
}
void progress(Canvas &c,const UiModel &m) {
    const double value=std::clamp(m.salary.progress,0.0,1.0);
    if (m.theme==DisplayTheme::Classic) {
        c.rect(12,148,252,4,LINE);c.rect(12,148,int(252*value),4,GOLD);
    } else {
        const int count=m.theme==DisplayTheme::Amber?28:21;
        const int step=252/count;
        for (int i=0;i<count;++i) {
            c.rect(12+i*step,147,step-2,6,LINE);
            const int filled=std::clamp(int(value*252)-i*step,0,step-2);
            if (filled) c.rect(12+i*step,147,filled,6,GOLD);
        }
    }
}
void schedule_transition(Canvas &c,const UiModel &m) {
    if (m.transition_progress>=1.f || m.held_ms>=500) return;
    const char *message;
    switch (m.transition_state) {
        case WORK_STATE_DAY_OFF:message="放假啦~";break;
        case WORK_STATE_WORKING_MORNING:message="開始上班~";break;
        case WORK_STATE_LUNCH:message="午餐時間!!!";break;
        case WORK_STATE_WORKING_AFTERNOON:message="繼續上班~~";break;
        case WORK_STATE_AFTER_WORK:message="下班啦~~~~~~~";break;
        default:return;
    }
    const float t=std::clamp(m.transition_progress,0.f,1.f);
    const float enter=std::clamp(t/.16f,0.f,1.f),exit=std::clamp((t-.82f)/.18f,0.f,1.f);
    // Back easing makes the large letters overshoot, then spring into place.
    const float u=enter-1.f;
    const float pop=1.f+2.70158f*u*u*u+1.70158f*u*u;
    const float fit=std::min(1.35f,272.f/Canvas::width(message,32));
    const float scale=fit*(.55f+.45f*pop)*(1.f-.12f*exit);
    const auto opacity=uint8_t(std::lround(15.f*std::min(1.f,t/.035f)*(1.f-exit)));
    const int bob=int(std::lround((1.f-enter)*13.f+std::sin(t*18.849556f)*1.5f));
    c.clip(8,31,304,114);
    c.rect(8,31,304,114,PANEL,opacity);
    c.rect(8,31,304,2,GOLD,opacity);c.rect(8,143,304,2,GOLD,opacity);
    // Small outward bursts underline the pop without flashing the whole screen.
    const int spread=int(std::lround(enter*17.f));
    for (int side:{-1,1}) {
        c.rect(160+side*(94+spread)-6,47,12,2,GOLD,opacity);
        c.rect(160+side*(112+spread),56,2,7,GREEN,opacity);
        c.rect(160+side*(80+spread)-4,126,8,2,GOLD,opacity);
    }
    c.zoom_text(160,84+bob,message,32,scale,GOLD,opacity);
    const char *caption=m.transition_state==WORK_STATE_DAY_OFF?"今天不用上班，好好放鬆":
        m.transition_state==WORK_STATE_LUNCH?"先吃飽，等等繼續偷":
        m.transition_state==WORK_STATE_AFTER_WORK?"今天辛苦了，好好休息":"準備好了，開始偷薪水";
    c.text(160-Canvas::width(caption,12)/2,112,caption,12,INK,280,opacity);
    c.clip();
}
void rtc_sync(Canvas &c,const UiModel &m) {
    if (!m.rtc.present || m.rtc.operation==RtcOperation::None || m.rtc_progress>=1.f ||
        m.held_ms>=500 || m.system==SYSTEM_SETUP_MODE || m.system==SYSTEM_ERROR) return;
    const float t=std::clamp(m.rtc_progress,0.f,1.f);
    const float enter=std::clamp(t/.16f,0.f,1.f),leave=std::clamp((t-.82f)/.18f,0.f,1.f);
    const float u=enter-1.f;
    const float pop=1.f+2.70158f*u*u*u+1.70158f*u*u;
    const auto opacity=uint8_t(std::lround(15.f*std::min(1.f,t/.035f)*(1.f-leave)));
    const bool reading=m.rtc.operation==RtcOperation::Read;
    const bool success=m.rtc.result==RtcResult::Success,failed=m.rtc.result==RtcResult::Failed;
    const uint16_t accent=success?GREEN:GOLD;
    c.clip(8,31,304,114);
    c.rect(8,31,304,114,PANEL,opacity);
    c.rect(8,31,304,2,accent,opacity);c.rect(8,143,304,2,accent,opacity);
    const char *title=failed?"RTC SYNC FAILED":reading?"RTC READ":"RTC SYNC";
    c.zoom_text(160,66+int((1.f-enter)*10.f),title,24,.70f+.30f*pop,accent,opacity);
    const char *direction=reading?"DS3231 -> CLOCK":"Wi-Fi -> DS3231";
    c.text(160-Canvas::width(direction,12)/2,88,direction,12,INK,280,opacity);
    // Chasing dots keep the short I2C transfer visibly animated for 3.2 seconds.
    const unsigned phase=m.animation_ms/120%5;
    for (unsigned i=0;i<5;++i) {
        const int lift=i==phase?3:0;
        c.rect(135+int(i)*11,110-lift,6,6,i==phase?accent:LINE,opacity);
    }
    const char *result=failed?(reading?"WAITING FOR NETWORK TIME":"WRITE FAILED / USING SYSTEM TIME"):
        success?(reading?"HWCLOCK READY":"RTC SAVED"):"SYNCHRONIZING...";
    c.text(160-Canvas::width(result,10)/2,126,result,10,accent,284,opacity);
    c.clip();
}
}
void ui_render(uint16_t *pixels,int offset,int rows,const UiModel &m) {
    const auto theme=display_theme_valid(static_cast<uint32_t>(m.theme))?m.theme:DisplayTheme::Classic;
    Canvas c(pixels,offset,rows,theme); c.rect(0,0,320,170,BG);
    frame(c,m);
    if (m.boot_progress<1.f && m.system!=SYSTEM_ERROR && m.held_ms<500) {
        boot_scene(c,m); return;
    }
    header(c,m); char buf[80];
    if (m.system==SYSTEM_SETUP_MODE) {
        c.text(14,37,"SETUP MODE",24,GOLD);
        c.text(14,74,"Wi-Fi",12,MUTED);c.text(62,73,m.ap_ssid[0]?m.ap_ssid:"SalaryThief-....",16,INK);
        c.text(14,100,"Open",12,MUTED);c.text(62,98,"192.168.4.1",24,INK);
        c.text(14,139,"連上設定網路，填好今天的偷薪計畫",12,MUTED);
    } else if (m.system==SYSTEM_ERROR) {
        c.text(14,42,"SYSTEM ERROR",24,GOLD);c.text(14,82,"請查看 USB 記錄並重新開機",16,INK);
    } else if (!m.synced) {
        if (!m.connected) {
            c.text(14,40,"CONNECTING Wi-Fi",24,GOLD);
            c.text(14,83,m.ssid[0]?m.ssid:"Waiting for Wi-Fi...",16,INK,292);
            c.text(14,112,m.associated?"Wi-Fi linked / waiting for IP":"Connecting to Wi-Fi...",12,MUTED);
            c.text(14,141,"連上網路後開始計算",12,MUTED);
        } else {
            c.text(14,40,"準備開始偷薪水",24,GOLD);
            c.text(14,83,"Waiting for time sync...",16,INK);
            c.text(14,112,"Wi-Fi connected / SNTP pending",12,MUTED);
            c.text(14,141,m.sntp_wait_expired?"同步尚未成功，請檢查網路":"時間同步後開始計算",12,MUTED);
        }
    } else if (m.salary.work_state==WORK_STATE_NO_CALENDAR) {
        c.text(14,42,"行事曆待更新",24,GOLD);
        c.text(14,81,"此年度尚未收錄，暫停薪資計算",16,INK);
        c.text(14,110,"請更新含該年度行事曆的韌體",12,MUTED);
        c.text(14,137,"時間仍持續運作",12,MUTED);
    } else {
        if (m.page==0) {
            c.rect(198,34,1,106,LINE);
            c.text(12,35,m.salary.work_state==WORK_STATE_AFTER_WORK?"今日偷到":
                m.salary.work_state==WORK_STATE_BEFORE_WORK?"還沒開偷":"今天已偷到",16,INK);
            c.text(12,59,"NT$",12,GREEN);
            money(c,12,74-int(m.pulse*2),m.salary.earned_money,182,GREEN);
            money_gain(c,m);
            if (m.salary.work_state==WORK_STATE_BEFORE_WORK) {
                duration(buf,sizeof(buf),m.salary.seconds_before_work);
                c.text(12,117,"距離上班",12,MUTED); c.text(71,117,buf,12,INK);
            } else c.text(12,118,work_label(m.salary.work_state),12,GREEN);
            c.clip(200,29,118,116);
            scene_frame(c,m);
            if (m.salary.work_state==WORK_STATE_DAY_OFF) holiday_scene(c,m.animation_ms);
            else if (m.salary.work_state==WORK_STATE_LUNCH) lunch_scene(c,m.animation_ms);
            else if (m.salary.work_state==WORK_STATE_AFTER_WORK) rest_scene(c,m.animation_ms);
            else {
                // Small scale marks keep the pile within a quiet instrument-like area.
                for (int y=51;y<140;y+=22) c.rect(307,y,5,1,LINE);
                c.rect(204,140,109,1,LINE);
                if (m.physics) for (const auto &v:m.physics->coins()) if(v.active) coin(c,v);
            }
            c.clip();
            progress(c,m);
            std::snprintf(buf,sizeof(buf),"%.0f%%",m.salary.progress*100); c.text(270,142,buf,12,INK);
            c.text(12,157,m.rtc.present?"HWCLOCK MODE":m.connected?"ONLINE":"OFFLINE / CLOCK RUNNING",10,MUTED);
        } else if (m.page==1) {
            c.text(12,37,"還能偷多少",16,INK);
            c.text(12,63,"今天還能偷 / NT$",12,MUTED); money(c,12,83,m.salary.remaining_money,188);
            c.rect(206,38,1,100,LINE);
            c.text(220,48,"剩餘工時",16,INK); duration(buf,sizeof(buf),m.salary.remaining_work_seconds);
            c.text(218,80,buf,16,GOLD);c.text(220,109,"已排除午休",12,MUTED);
            c.text(12,142,work_label(m.salary.work_state),12,GREEN);
        } else if (m.page==2) {
            c.text(12,35,"本月戰績",16,INK);
            std::snprintf(buf,sizeof(buf),"NT$ %lu",static_cast<unsigned long>(m.config.monthly_salary));
            c.text(12,56,"月薪",12,MUTED); c.text(12,72,buf,16,GOLD);
            std::snprintf(buf,sizeof(buf),"%d / %d",m.salary.work_day_index,m.salary.monthly_work_days);
            c.text(12,97,"今天 / 本月工作日",12,MUTED); c.text(12,113,buf,16,INK);
            std::snprintf(buf,sizeof(buf),"%.2f h",double(m.salary.monthly_work_seconds)/3600);
            c.text(12,141,"工時",12,MUTED);c.text(48,139,buf,16,GOLD,114);
            c.rect(166,37,1,113,LINE);
            const char *labels[]={"每日","每小時","每分鐘","每秒"};
            const double values[]={m.salary.daily_salary,m.salary.salary_per_second*3600,m.salary.salary_per_second*60,m.salary.salary_per_second};
            for(int i=0;i<4;++i) {
                c.text(179,39+i*29,labels[i],12,MUTED);
                std::snprintf(buf,sizeof(buf),i==3?"NT$ %.4f":"NT$ %.2f",values[i]);
                const int size=Canvas::width(buf,12)>132 ? 10 : 12;
                c.text(179,53+i*29,buf,size,INK,132);
            }
        } else {
            c.text(12,33,"系統資訊",16,INK);
            const char *rtc_label=m.rtc.present?"RTC:Enable":"RTC:None";
            c.text(306-Canvas::width(rtc_label,12),35,rtc_label,12,m.rtc.present?GREEN:MUTED);
            c.text(12,55,"SSID",10,MUTED);c.text(52,53,m.ssid,12,INK,254);
            std::snprintf(buf,sizeof(buf),"IP %s   RSSI %d dBm",m.ip,m.rssi);c.text(12,69,buf,10,INK);
            std::snprintf(buf,sizeof(buf),"SNTP %s   IDF %s",m.sntp_synced?"SYNCED":"WAITING",m.idf);c.text(12,82,buf,10,INK);
            std::snprintf(buf,sizeof(buf),"FW %s   Config v%lu   Up %lus",m.firmware,static_cast<unsigned long>(m.config.version),static_cast<unsigned long>(m.uptime));c.text(12,95,buf,10,INK);
            std::snprintf(buf,sizeof(buf),"Heap %luK   PSRAM %luK   %s",static_cast<unsigned long>(m.free_heap/1024),static_cast<unsigned long>(m.free_psram/1024),m.partial?"PARTIAL":"DOUBLE");c.text(12,108,buf,10,INK);
            std::snprintf(buf,sizeof(buf),"Frame %luus   Missed %lu",static_cast<unsigned long>(m.frame_us),static_cast<unsigned long>(m.dropped_frames));c.text(12,121,buf,10,MUTED);
            battery_info(c,m.battery);
            c.text(12,152,"2 clicks: Setup / Hold 5s: Sleep",10,GOLD);
        }
        footer(c,m);
        schedule_transition(c,m);
    }
    rtc_sync(c,m);
    if (m.held_ms>=500) {
        c.rect(40,36,240,105,PANEL);c.rect(40,36,240,2,GOLD);
        c.center(160,49,m.held_ms<5000 ? "HOLD TO SLEEP" : "RELEASE TO SLEEP",16,INK);
        std::snprintf(buf,sizeof(buf),"%lu",static_cast<unsigned long>(m.held_ms>=5000 ? 0 : (5000-m.held_ms+999)/1000));
        c.center(160,78,buf,32,GOLD);
    }
}
