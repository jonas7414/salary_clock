#include "calendar_download.h"
#include <cstring>

namespace {
class Parser {
    CalendarRead read_; void *context_;
    int next_{-3}; size_t bytes_{};
    int peek() {
        if (next_==-3) next_=++bytes_>160*1024 ? -2 : read_(context_);
        return next_;
    }
    int take() { const int c=peek(); next_=-3; return c; }
    void space() { while (peek()==' ' || peek()=='\r' || peek()=='\n' || peek()=='\t') take(); }
    bool eat(int c) { space(); if (peek()!=c) return false; take(); return true; }
    static bool digit(int c) { return c>='0' && c<='9'; }
    bool literal(const char *text) {
        space(); while (*text) if (take()!=*text++) return false; return true;
    }
    bool string(char *out=nullptr,size_t capacity=0) {
        if (!eat('"')) return false;
        size_t length=0;
        while (true) {
            int c=take();
            if (c<32) return false;
            if (c=='"') { if (out) out[length]=0; return true; }
            if (c=='\\') {
                c=take();
                if (c=='u') {
                    unsigned value=0;
                    for (int i=0;i<4;++i) {
                        const int h=take();
                        if (h>='0' && h<='9') value=value*16+unsigned(h-'0');
                        else if (h>='a' && h<='f') value=value*16+unsigned(h-'a'+10);
                        else if (h>='A' && h<='F') value=value*16+unsigned(h-'A'+10);
                        else return false;
                    }
                    // Relevant keys and dates are ASCII. Non-ASCII metadata is skipped.
                    if (out && (value<32 || value>126)) return false;
                    c=int(value);
                } else if (c!='"' && c!='\\' && c!='/' && c!='b' && c!='f' && c!='n' && c!='r' && c!='t') return false;
            }
            if (out) {
                if (length+1>=capacity) return false;
                out[length++]=char(c);
            }
        }
    }
    bool number() {
        space(); if (peek()=='-') take();
        if (peek()=='0') take();
        else { if (peek()<'1' || peek()>'9') return false; while (digit(peek())) take(); }
        if (peek()=='.') { take(); if (!digit(peek())) return false; while (digit(peek())) take(); }
        if (peek()=='e' || peek()=='E') {
            take(); if (peek()=='+' || peek()=='-') take();
            if (!digit(peek())) return false;
            while (digit(peek())) take();
        }
        return true;
    }
    bool integer(int &out) {
        space(); if (!digit(peek())) return false;
        out=take()-'0';
        if (!out && digit(peek())) return false;
        while (digit(peek())) { out=out*10+take()-'0'; if (out>9999) return false; }
        return true;
    }
    bool skip(unsigned depth=0) {
        if (depth>12) return false;
        space(); const int c=peek();
        if (c=='"') return string();
        if (c=='t') return literal("true");
        if (c=='f') return literal("false");
        if (c=='n') return literal("null");
        if (c=='{' || c=='[') {
            take(); const int end=c=='{' ? '}' : ']';
            if (eat(end)) return true;
            do {
                if (c=='{' && (!string() || !eat(':'))) return false;
                if (!skip(depth+1)) return false;
                if (eat(end)) return true;
            } while (eat(','));
            return false;
        }
        return number();
    }
    bool day(int year,int &month,int &day,bool &holiday) {
        if (!eat('{')) return false;
        unsigned fields=0; char key[64],date[16]{};
        do {
            if (!string(key,sizeof(key)) || !eat(':')) return false;
            if (!std::strcmp(key,"date")) {
                if ((fields&1) || !string(date,sizeof(date))) return false;
                fields|=1;
            } else if (!std::strcmp(key,"isHoliday")) {
                if (fields&2) return false;
                space(); holiday=peek()=='t';
                if (!literal(holiday ? "true" : "false")) return false;
                fields|=2;
            } else if (!skip()) return false;
            if (eat('}')) break;
            if (!eat(',')) return false;
        } while (true);
        if (fields!=3 || std::strlen(date)!=8) return false;
        int value=0;
        for (int i=0;i<8;++i) { if (!digit(date[i])) return false; value=value*10+date[i]-'0'; }
        month=value/100%100; day=value%100;
        return value/10000==year && month>=1 && month<=12 && day>=1 && day<=calendar_days_in_month(year,month);
    }
    bool month(int year,int &index,uint32_t &workdays) {
        if (!eat('{')) return false;
        unsigned fields=0; uint32_t seen=0; int date_month=0; char key[64];
        do {
            if (!string(key,sizeof(key)) || !eat(':')) return false;
            if (!std::strcmp(key,"month")) {
                if ((fields&1) || !integer(index) || index<1 || index>12) return false;
                fields|=1;
            } else if (!std::strcmp(key,"holidays")) {
                if ((fields&2) || !eat('[')) return false;
                fields|=2;
                do {
                    int m=0,d=0; bool holiday=false;
                    if (!day(year,m,d,holiday) || (date_month && date_month!=m)) return false;
                    date_month=m; const uint32_t bit=1U<<(d-1);
                    if (seen&bit) return false;
                    seen|=bit; if (!holiday) workdays|=bit;
                    if (eat(']')) break;
                    if (!eat(',')) return false;
                } while (true);
            } else if (!skip()) return false;
            if (eat('}')) break;
            if (!eat(',')) return false;
        } while (true);
        return fields==3 && index==date_month && seen==((1U<<calendar_days_in_month(year,index))-1);
    }
public:
    Parser(CalendarRead read,void *context):read_(read),context_(context) {}
    bool parse(int expected,CalendarYear &output) {
        if (expected<2026 || expected>9999 || !eat('{')) return false;
        CalendarYear result{}; result.year=expected;
        unsigned fields=0,seen=0; char key[64];
        do {
            if (!string(key,sizeof(key)) || !eat(':')) return false;
            if (!std::strcmp(key,"year")) {
                int value=0;
                if ((fields&1) || !integer(value) || value!=expected) return false;
                fields|=1;
            } else if (!std::strcmp(key,"months")) {
                if ((fields&2) || !eat('[')) return false;
                fields|=2;
                do {
                    int index=0; uint32_t workdays=0;
                    if (!month(expected,index,workdays) || (seen&(1U<<index))) return false;
                    seen|=1U<<index; result.workdays[index-1]=workdays;
                    if (eat(']')) break;
                    if (!eat(',')) return false;
                } while (true);
            } else if (!skip()) return false;
            if (eat('}')) break;
            if (!eat(',')) return false;
        } while (true);
        space();
        if (fields!=3 || seen!=0x1ffe || peek()!=-1) return false;
        output=result; return true;
    }
};
}
bool calendar_parse_year(CalendarRead read,void *context,int year,CalendarYear &output) {
    return read && Parser(read,context).parse(year,output);
}
