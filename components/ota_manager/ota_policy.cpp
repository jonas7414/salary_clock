#include "ota_policy.h"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ota {
namespace {
bool digit(char ch) { return ch>='0' && ch<='9'; }
bool identifier(char ch) { return digit(ch) || (ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || ch=='-'; }
bool number(const char *&p,uint32_t &out) {
    if (!digit(*p) || (*p=='0' && digit(p[1]))) return false;
    out=0;
    while (digit(*p)) {
        const unsigned n=unsigned(*p++-'0');
        if (out>(UINT32_MAX-n)/10U) return false;
        out=out*10U+n;
    }
    return true;
}
bool identifiers(const char *&p,bool prerelease) {
    do {
        const char *start=p; bool numeric=true;
        while (identifier(*p)) { numeric&=digit(*p); ++p; }
        if (p==start || (prerelease && numeric && p-start>1 && *start=='0')) return false;
        if (*p!='.') return true;
        ++p;
    } while (true);
}
int compare_identifier(const char *a,size_t na,const char *b,size_t nb) {
    bool da=true,db=true;
    for (size_t i=0;i<na;++i) da&=digit(a[i]);
    for (size_t i=0;i<nb;++i) db&=digit(b[i]);
    if (da!=db) return da?-1:1;
    if (da && na!=nb) return na<nb?-1:1;
    const int cmp=std::memcmp(a,b,std::min(na,nb));
    if (cmp) return cmp<0?-1:1;
    return na==nb?0:na<nb?-1:1;
}
bool unique_keys(const cJSON *object) {
    if (!cJSON_IsObject(object)) return false;
    for (auto *a=object->child;a;a=a->next)
        for (auto *b=a->next;b;b=b->next)
            if (std::strcmp(a->string,b->string)==0) return false;
    return true;
}
const char *string(const cJSON *j,const char *key) {
    const auto *v=cJSON_GetObjectItemCaseSensitive(j,key);
    return cJSON_IsString(v)?v->valuestring:nullptr;
}
bool copy(char *dest,size_t capacity,const char *source) {
    if (!source || std::strlen(source)>=capacity) return false;
    std::strcpy(dest,source); return true;
}
bool asset_url(const char *url,const char *repo,const char *tag,const char *asset) {
    char expected[URL_CAPACITY];
    const int n=std::snprintf(expected,sizeof(expected),"https://github.com/%s/releases/download/%s/%s",repo,tag,asset);
    return n>0 && size_t(n)<sizeof(expected) && url && std::strcmp(url,expected)==0;
}
bool safe_json(const char *text,size_t length) {
    if (!text || !length || length>MAX_RELEASE_JSON || std::memchr(text,0,length)) return false;
    bool quoted=false,escaped=false; unsigned depth=0;
    for (size_t i=0;i<length;++i) {
        const char ch=text[i];
        if (quoted) {
            if (escaped) { escaped=false; continue; }
            if (ch=='\\') {
                if (i+5<length && std::memcmp(text+i,"\\u0000",6)==0) return false;
                escaped=true;
            } else if (ch=='"') quoted=false;
        } else if (ch=='"') quoted=true;
        else if (ch=='{' || ch=='[') { if (++depth>12) return false; }
        else if (ch=='}' || ch==']') { if (!depth) return false; --depth; }
    }
    return !quoted && !depth;
}
}
bool parse_version(const char *text,Version &out) {
    if (!text || !*text) return false;
    const char *p=text; if (*p=='v') ++p;
    if (std::strlen(p)>=VERSION_CAPACITY) return false;
    Version parsed{};
    if (!number(p,parsed.major) || *p++!='.' || !number(p,parsed.minor) || *p++!='.' || !number(p,parsed.patch)) return false;
    if (*p=='-') {
        const char *start=++p;
        if (!identifiers(p,true)) return false;
        std::memcpy(parsed.prerelease,start,size_t(p-start));
    }
    if (*p=='+') { ++p; if (!identifiers(p,false)) return false; }
    if (*p) return false;
    out=parsed;return true;
}
int compare_versions(const Version &a,const Version &b) {
    if (a.major!=b.major) return a.major<b.major?-1:1;
    if (a.minor!=b.minor) return a.minor<b.minor?-1:1;
    if (a.patch!=b.patch) return a.patch<b.patch?-1:1;
    const char *pa=a.prerelease,*pb=b.prerelease;
    if (!*pa || !*pb) return *pa?-1:*pb?1:0;
    while (true) {
        const size_t na=std::strcspn(pa,"."),nb=std::strcspn(pb,".");
        const int cmp=compare_identifier(pa,na,pb,nb); if (cmp) return cmp;
        pa+=na;pb+=nb;
        if (!*pa || !*pb) return *pa?1:*pb?-1:0;
        ++pa;++pb;
    }
}
bool https_url_allowed(const char *url) {
    if (!url || std::strncmp(url,"https://",8)!=0 || std::strlen(url)>2047) return false;
    for (const char *p=url;*p;++p) if (static_cast<unsigned char>(*p)<=32 || *p=='\\' || *p=='#') return false;
    const char *host=url+8; const size_t n=std::strcspn(host,"/?");
    // Exact hosts, no userinfo, custom ports or suffix-based lookalikes.
    const char *allowed[]={"api.github.com","github.com","release-assets.githubusercontent.com",
                           "objects.githubusercontent.com","github-releases.githubusercontent.com"};
    for (const auto *name:allowed) if (std::strlen(name)==n && std::memcmp(host,name,n)==0) return true;
    return false;
}
bool parse_sha256(const char *text,uint8_t output[32]) {
    if (!text || std::strlen(text)!=64) return false;
    const auto nibble=[](char c)->int { return digit(c)?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; };
    uint8_t digest[32];
    for (unsigned i=0;i<32;++i) {
        const int a=nibble(text[i*2]),b=nibble(text[i*2+1]);
        if (a<0 || b<0) return false;
        digest[i]=uint8_t(a*16+b);
    }
    std::memcpy(output,digest,32);return true;
}
bool parse_checksum_file(const char *text,size_t length,const char *asset,uint8_t output[32]) {
    if (!text || !asset || length<64 || length>256 || std::memchr(text,0,length)) return false;
    char hex[65]{};std::memcpy(hex,text,64);
    const char *p=text+64,*end=text+length;
    if (p<end && *p==' ') {
        while (p<end && *p==' ') ++p;
        if (p<end && *p=='*') ++p;
        const size_t n=std::strlen(asset);
        if (size_t(end-p)<n || std::memcmp(p,asset,n)!=0) return false;
        p+=n;
    }
    while (p<end && (*p=='\r' || *p=='\n')) ++p;
    return p==end && parse_sha256(hex,output);
}
bool parse_release(const char *body,size_t length,const char *repository,const char *asset,Release &out,const char **reason) {
    *reason="Malformed or oversized release JSON";
    if (!repository || !asset || !safe_json(body,length)) return false;
    const char *end=nullptr;
    cJSON *json=cJSON_ParseWithLengthOpts(body,length,&end,false);
    if (!json) return false;
    struct Cleanup { cJSON *j; ~Cleanup(){cJSON_Delete(j);} } cleanup{json};
    while (end<body+length && (*end==' ' || *end=='\r' || *end=='\n' || *end=='\t')) ++end;
    if (end!=body+length || !unique_keys(json)) return false;
    if (!cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json,"draft")) ||
        !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json,"prerelease"))) {
        *reason="Release is draft, prerelease, or missing release flags"; return false;
    }
    const char *tag=string(json,"tag_name"); Version version{}; Release release{};
    if (!parse_version(tag,version) || version.prerelease[0] || !copy(release.version,sizeof(release.version),tag[0]=='v'?tag+1:tag)) {
        *reason="Malformed or non-stable release version";return false;
    }
    char checksum_name[80];
    const size_t asset_length=std::strlen(asset);
    if (asset_length<4 || asset_length>=sizeof(checksum_name)-4 || std::strcmp(asset+asset_length-4,".bin")!=0) return false;
    std::snprintf(checksum_name,sizeof(checksum_name),"%.*s.sha256",int(asset_length-4),asset);
    const auto *assets=cJSON_GetObjectItemCaseSensitive(json,"assets");
    if (!cJSON_IsArray(assets)) { *reason="Release assets missing"; return false; }
    const cJSON *firmware=nullptr,*checksum=nullptr;
    for (auto *item=assets->child;item;item=item->next) {
        if (!unique_keys(item)) return false;
        const char *name=string(item,"name"); if (!name) return false;
        if (std::strcmp(name,asset)==0) { if (firmware) return false; firmware=item; }
        if (std::strcmp(name,checksum_name)==0) { if (checksum) return false; checksum=item; }
    }
    if (!firmware) { *reason="Firmware asset missing";return false; }
    const char *url=string(firmware,"browser_download_url");
    const auto *size=cJSON_GetObjectItemCaseSensitive(firmware,"size");
    if (!asset_url(url,repository,tag,asset) || !copy(release.firmware_url,sizeof(release.firmware_url),url) ||
        !cJSON_IsNumber(size) || !std::isfinite(size->valuedouble) || size->valuedouble<288 || size->valuedouble>UINT32_MAX ||
        std::floor(size->valuedouble)!=size->valuedouble) { *reason="Invalid firmware URL or size";return false; }
    release.size=uint32_t(size->valuedouble);
    const auto *digest=cJSON_GetObjectItemCaseSensitive(firmware,"digest");
    if (digest && !cJSON_IsNull(digest)) {
        if (!cJSON_IsString(digest) || std::strncmp(digest->valuestring,"sha256:",7)!=0 || !parse_sha256(digest->valuestring+7,release.sha256)) {
            *reason="Invalid asset SHA256 digest";return false;
        }
        release.has_sha256=true;
    }
    if (checksum) {
        const char *checksum_url=string(checksum,"browser_download_url");
        if (!asset_url(checksum_url,repository,tag,checksum_name) || !copy(release.checksum_url,sizeof(release.checksum_url),checksum_url)) {
            *reason="Invalid checksum asset URL";return false;
        }
    }
    if (!release.has_sha256 && !release.checksum_url[0]) { *reason="Release SHA256 missing";return false; }
    out=release;*reason=nullptr;return true;
}
}
