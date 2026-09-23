#include "config_json.h"
#include "cJSON.h"
#include <cmath>
#include <cstring>
#include <initializer_list>
namespace {
bool number(cJSON *j,const char *key,uint32_t &out,uint32_t maximum) {
    const auto *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<0 ||
        v->valuedouble>maximum || std::floor(v->valuedouble)!=v->valuedouble) return false;
    out=static_cast<uint32_t>(v->valuedouble); return true;
}
bool string(cJSON *j,const char *key,char *out,size_t capacity) {
    const auto *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if (!cJSON_IsString(v) || std::strlen(v->valuestring)>=capacity) return false;
    std::strcpy(out,v->valuestring); return true;
}
bool get_time(cJSON *j,const char *key,uint16_t &minutes) {
    char v[6]; if (!string(j,key,v,sizeof(v)) || std::strlen(v)!=5 || v[2]!=':') return false;
    for (int i : {0,1,3,4}) if (v[i]<'0' || v[i]>'9') return false;
    const int h=(v[0]-'0')*10+v[1]-'0', m=(v[3]-'0')*10+v[4]-'0';
    if (h>23 || m>59) return false;
    minutes=h*60+m; return true;
}
}
bool config_parse_json(const char *body,size_t length,const AppConfig &current,AppConfig &result,const char **reason) {
    *reason="Invalid JSON object";
    if (!body || !length || length>2048 || std::memchr(body,0,length)) return false;
    // This API is a flat object. Reject nested containers before cJSON recursion can
    // consume the HTTP task's bounded stack, even within a small request body.
    bool quoted=false,escaped=false; unsigned depth=0;
    for (size_t i=0;i<length;++i) {
        const char ch=body[i];
        if (quoted) {
            if (escaped) { escaped=false; continue; }
            if (ch=='\\') {
                if (i+5<length && std::memcmp(body+i,"\\u0000",6)==0) return false;
                escaped=true;
            } else if (ch=='"') quoted=false;
        } else if (ch=='"') quoted=true;
        else if (ch=='[' || ch==']') return false;
        else if (ch=='{' && ++depth>1) return false;
        else if (ch=='}') { if (!depth) return false; --depth; }
    }
    const char *end=nullptr;
    cJSON *j=cJSON_ParseWithLengthOpts(body,length,&end,false);
    if (!cJSON_IsObject(j)) { cJSON_Delete(j); return false; }
    // Accept trailing JSON whitespace only; no concatenated objects or embedded NUL.
    while (end<body+length && (*end==' ' || *end=='\r' || *end=='\n' || *end=='\t')) ++end;
    if (end!=body+length) { cJSON_Delete(j); return false; }
    for (auto *a=j->child;a;a=a->next) for (auto *b=a->next;b;b=b->next)
        if (std::strcmp(a->string,b->string)==0) { cJSON_Delete(j); *reason="Duplicate JSON key"; return false; }
    auto c=current; uint32_t mask=0,version=0;
    bool valid=number(j,"config_version",version,UINT32_MAX) && version==CONFIG_VERSION &&
        number(j,"monthly_salary",c.monthly_salary,1000000000) && number(j,"work_days",mask,127) &&
        string(j,"wifi_ssid",c.wifi_ssid,sizeof(c.wifi_ssid)) && string(j,"timezone",c.timezone,sizeof(c.timezone)) &&
        get_time(j,"work_start",c.work_start) && get_time(j,"lunch_start",c.lunch_start) &&
        get_time(j,"lunch_end",c.lunch_end) && get_time(j,"work_end",c.work_end);
    c.work_days=mask;
    if (cJSON_HasObjectItem(j,"wifi_password")) valid=string(j,"wifi_password",c.wifi_password,sizeof(c.wifi_password)) && valid;
    else if (std::strcmp(c.wifi_ssid,current.wifi_ssid)!=0) valid=false;
    cJSON_Delete(j);
    if (!valid) { *reason="Missing or invalid settings; supply password when changing SSID"; return false; }
    if (!config_validate(c,true,reason)) return false;
    result=c; *reason=nullptr; return true;
}
