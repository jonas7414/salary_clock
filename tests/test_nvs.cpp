#include "app_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static std::vector<unsigned char> committed,pending;
static bool fail_commit=false;static int flash_error=0;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"NVS FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
esp_err_t nvs_flash_init(){int err=flash_error;flash_error=0;return err;}
esp_err_t nvs_flash_erase(){committed.clear();pending.clear();return ESP_OK;}
esp_err_t nvs_open(const char *,int mode,nvs_handle_t *h){*h=1;return mode==NVS_READONLY&&committed.empty()?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t,const char *,void *out,size_t *size){if(committed.empty())return ESP_ERR_NVS_NOT_FOUND;if(*size<committed.size())return ESP_FAIL;*size=committed.size();std::memcpy(out,committed.data(),*size);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *data,size_t size){auto p=static_cast<const unsigned char*>(data);pending.assign(p,p+size);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t){if(fail_commit)return ESP_FAIL;committed=pending;return ESP_OK;}
esp_err_t nvs_erase_all(nvs_handle_t){pending.clear();return ESP_OK;}
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
    committed=good;flash_error=ESP_ERR_NVS_NEW_VERSION_FOUND;CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    committed=good;flash_error=ESP_ERR_NVS_NO_FREE_PAGES;CHECK(app_config_init(&loaded)==ESP_OK&&!loaded);
    std::printf("PASS: %u NVS checks (first boot, persistence, corruption, version mismatch, commit failure, reset, recovery)\n",checks);
}
