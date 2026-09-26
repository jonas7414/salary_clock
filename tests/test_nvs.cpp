#include "app_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <string>
static std::vector<unsigned char> committed,pending,committed_theme,pending_theme;
static std::vector<unsigned char> committed_schedule,pending_schedule;
static std::map<std::string,std::vector<unsigned char>> extras,pending_extras;
static bool fail_commit=false;static int flash_error=0;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"NVS FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
esp_err_t nvs_flash_init(){int err=flash_error;flash_error=0;return err;}
esp_err_t nvs_flash_erase(){committed.clear();pending.clear();committed_theme.clear();pending_theme.clear();committed_schedule.clear();pending_schedule.clear();extras.clear();pending_extras.clear();return ESP_OK;}
esp_err_t nvs_open(const char *,int mode,nvs_handle_t *h){*h=1;if(mode!=NVS_READONLY){pending=committed;pending_theme=committed_theme;pending_schedule=committed_schedule;pending_extras=extras;}return mode==NVS_READONLY&&committed.empty()?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t,const char *key,void *out,size_t *size){const auto &v=std::strcmp(key,"theme")==0?committed_theme:std::strcmp(key,"display_hours")==0?committed_schedule:std::strcmp(key,"config")==0?committed:extras[key];if(v.empty())return ESP_ERR_NVS_NOT_FOUND;if(*size<v.size())return ESP_FAIL;*size=v.size();std::memcpy(out,v.data(),*size);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t,const char *key,const void *data,size_t size){auto p=static_cast<const unsigned char*>(data);auto &v=std::strcmp(key,"theme")==0?pending_theme:std::strcmp(key,"display_hours")==0?pending_schedule:std::strcmp(key,"config")==0?pending:pending_extras[key];v.assign(p,p+size);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t){if(fail_commit)return ESP_FAIL;committed=pending;committed_theme=pending_theme;committed_schedule=pending_schedule;extras=pending_extras;return ESP_OK;}
esp_err_t nvs_erase_all(nvs_handle_t){pending.clear();pending_theme.clear();pending_schedule.clear();pending_extras.clear();return ESP_OK;}
void nvs_close(nvs_handle_t){}
int main(){
    bool loaded=true;CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    auto c=app_config_snapshot();CHECK(c.monthly_salary==40000);std::strcpy(c.wifi_ssid,"Test AP");std::strcpy(c.wifi_password,"test-password");
    c.monthly_salary=55000;CHECK(app_config_save(c)==ESP_OK);CHECK(app_config_snapshot().monthly_salary==40000);
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);CHECK(app_config_snapshot().monthly_salary==55000);
    CHECK(std::strcmp(app_config_snapshot().wifi_password,"test-password")==0);
    auto good=committed;committed.back()^=0x01;CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    committed=good;committed.resize(12);CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    committed=good;
    // Record's config is after three uint32_t fields. Correct CRC, incompatible version.
    AppConfig mismatch{};std::memcpy(&mismatch,committed.data()+12,sizeof(mismatch));mismatch.version++;
    const uint32_t crc=config_checksum(mismatch);std::memcpy(committed.data()+8,&crc,4);std::memcpy(committed.data()+12,&mismatch,sizeof(mismatch));
    CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    committed=good;fail_commit=true;c.monthly_salary=65000;CHECK(app_config_save(c)==ESP_FAIL);fail_commit=false;
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);CHECK(app_config_snapshot().monthly_salary==55000);
    CHECK(app_config_reset()==ESP_OK);CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    committed=good;flash_error=ESP_ERR_NVS_NEW_VERSION_FOUND;CHECK(app_config_init(&loaded)==ESP_ERR_NVS_NEW_VERSION_FOUND);CHECK(committed==good);
    committed=good;flash_error=ESP_ERR_NVS_NO_FREE_PAGES;CHECK(app_config_init(&loaded)==ESP_ERR_NVS_NO_FREE_PAGES);CHECK(committed==good);
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);CHECK(app_config_verify()==ESP_OK);
    CHECK(app_config_begin_ota());CHECK(!app_config_begin_ota());
    CHECK(app_config_save(c)==ESP_ERR_INVALID_STATE);CHECK(app_config_reset()==ESP_ERR_INVALID_STATE);
    CHECK(app_config_snapshot().monthly_salary==55000);CHECK(committed==good);
    app_config_end_ota();CHECK(app_config_save(c)==ESP_OK);
    committed.back()^=1;CHECK(app_config_verify()!=ESP_OK);
    // Upgrade from v1.2.x: the original blob and CRC remain byte-for-byte intact.
    committed=good;committed_theme.clear();
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);CHECK(app_config_theme()==DisplayTheme::Classic);
    CHECK(committed==good&&committed_theme.empty());CHECK(app_config_verify()==ESP_OK);
    c=app_config_snapshot();
    for (auto theme:{DisplayTheme::Amber,DisplayTheme::Handheld,DisplayTheme::Classic}) {
        const auto previous=app_config_theme();
        CHECK(app_config_save(c,theme)==ESP_OK);CHECK(committed==good);
        CHECK(app_config_theme()==previous); // Applied together with settings on reboot.
        CHECK(app_config_init(&loaded)==ESP_OK&&loaded);CHECK(app_config_theme()==theme);
        CHECK(app_config_verify()==ESP_OK);
    }
    CHECK(app_config_save(c,DisplayTheme::Amber)==ESP_OK);
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    fail_commit=true;CHECK(app_config_save(c,DisplayTheme::Handheld)==ESP_FAIL);fail_commit=false;
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);CHECK(app_config_theme()==DisplayTheme::Amber);
    CHECK(app_config_save(c)==ESP_OK);CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    CHECK(app_config_theme()==DisplayTheme::Amber);
    CHECK(app_config_save(c,static_cast<DisplayTheme>(3))==ESP_ERR_INVALID_ARG);
    CHECK(app_config_begin_ota());CHECK(app_config_save(c,DisplayTheme::Handheld)==ESP_ERR_INVALID_STATE);app_config_end_ota();
    for (const auto &bytes:{std::vector<unsigned char>{255,0,0,0},std::vector<unsigned char>{1}}) {
        committed_theme=bytes;CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
        CHECK(app_config_theme()==DisplayTheme::Classic);CHECK(committed==good);
    }
    CHECK(app_config_reset()==ESP_OK);CHECK(committed_theme.empty());
    CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);CHECK(app_config_theme()==DisplayTheme::Classic);
    // Old firmware leaves the display schedule absent; loading it never rewrites NVS.
    committed=good;committed_theme.clear();committed_schedule.clear();
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    CHECK(app_config_display_schedule().on_minute==480 && app_config_display_schedule().off_minute==1140);
    CHECK(committed==good && committed_schedule.empty());
    CHECK(app_config_save(c,DisplayTheme::Amber,{22*60,7*60+30})==ESP_OK);
    CHECK(committed==good); // The v1 config record still has exactly the same layout and CRC.
    CHECK(app_config_display_schedule().on_minute==480); // Applied on reboot.
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    CHECK(app_config_display_schedule().on_minute==1320 && app_config_display_schedule().off_minute==450);
    CHECK(app_config_theme()==DisplayTheme::Amber);
    CHECK(app_config_save(c)==ESP_OK);CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    CHECK(app_config_display_schedule().on_minute==1320);
    CHECK(app_config_save(c,DisplayTheme::Handheld)==ESP_OK);CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    CHECK(app_config_display_schedule().off_minute==450);
    fail_commit=true;CHECK(app_config_save(c,DisplayTheme::Classic,{600,1200})==ESP_FAIL);fail_commit=false;
    CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    CHECK(app_config_display_schedule().on_minute==1320 && app_config_theme()==DisplayTheme::Handheld);
    CHECK(app_config_save(c,DisplayTheme::Classic,{1440,1200})==ESP_ERR_INVALID_ARG);
    CHECK(app_config_save(c,DisplayTheme::Classic,{600,1440})==ESP_ERR_INVALID_ARG);
    CHECK(app_config_begin_ota());
    CHECK(app_config_save(c,DisplayTheme::Classic,{600,1200})==ESP_ERR_INVALID_STATE);app_config_end_ota();
    for (const auto &bytes:{std::vector<unsigned char>{0xff,0xff,0,0},std::vector<unsigned char>{1},
                           std::vector<unsigned char>{0,0,0,0,0}}) {
        committed_schedule=bytes;CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
        CHECK(app_config_display_schedule().on_minute==480 && app_config_display_schedule().off_minute==1140);
        CHECK(committed==good && committed_schedule==bytes);
    }
    CHECK(app_config_save(c,DisplayTheme::Classic,{0,0})==ESP_OK);CHECK(app_config_init(&loaded)==ESP_OK&&loaded);
    CHECK(app_config_display_schedule().on_minute==0 && app_config_display_schedule().off_minute==0);
    CHECK(app_config_reset()==ESP_OK);CHECK(committed_schedule.empty());
    CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    CHECK(app_config_display_schedule().on_minute==480 && app_config_display_schedule().off_minute==1140);
    DisplayPreferences prefs{};std::strcpy(prefs.order,"543210");std::strcpy(prefs.anniversary_name,"Our day");std::strcpy(prefs.anniversary_date,"2020-02-29");
    CHECK(app_config_save(c,DisplayTheme::Classic,{},prefs)==ESP_OK);
    CHECK(std::strcmp(app_config_display_preferences().order,"012453")==0);
    CHECK(app_config_init(&loaded)==ESP_OK && loaded);
    CHECK(std::strcmp(app_config_display_preferences().order,"543210")==0);
    CHECK(std::strcmp(app_config_display_preferences().anniversary_name,"Our day")==0);
    const auto base_config=committed;
    CHECK(app_config_ignore_version("1.5.0")==ESP_OK);char ignored[32]{};app_config_ignored_version(ignored);
    CHECK(std::strcmp(ignored,"1.5.0")==0 && committed==base_config);
    CHECK(app_config_init(&loaded)==ESP_OK && loaded);app_config_ignored_version(ignored);
    CHECK(std::strcmp(ignored,"1.5.0")==0);
    fail_commit=true;CHECK(app_config_ignore_version("1.6.0")==ESP_FAIL);fail_commit=false;
    app_config_ignored_version(ignored);CHECK(std::strcmp(ignored,"1.5.0")==0);
    CHECK(app_config_save(c)==ESP_OK);CHECK(app_config_init(&loaded)==ESP_OK && loaded);
    app_config_ignored_version(ignored);CHECK(std::strcmp(ignored,"1.5.0")==0);
    CHECK(std::strcmp(app_config_display_preferences().order,"543210")==0);
    extras["display_prefs"]={1,2,3};CHECK(app_config_init(&loaded)==ESP_OK && loaded);
    CHECK(std::strcmp(app_config_display_preferences().order,"012453")==0);
    CHECK(app_config_reset()==ESP_OK);CHECK(app_config_init(&loaded)==ESP_OK && !loaded);
    app_config_ignored_version(ignored);CHECK(!ignored[0]);
    std::printf("PASS: %u NVS checks (persistence, corruption, migration, failures, preferences, ignored versions, OTA exclusion)\n",checks);
}
