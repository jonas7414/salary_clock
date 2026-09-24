#include "ds3231.h"
#include "ds3231_time.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned checks=0;
#define CHECK(x) do { ++checks;if (!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1); } } while(0)
namespace {
uint8_t chip[19]{};
bool attached=true,bus_active=false,device_active=false;
int transfers=0,fail_at=0;
bool corrupt_time=false;
constexpr time_t SAMPLE=1790253296; // 2026-09-24 12:34:56 UTC, Thursday.
void reset_chip() {
    std::memset(chip,0,sizeof(chip));
    CHECK(ds3231_encode_time(SAMPLE,chip));
    chip[0x0e]=0x1c;chip[0x0f]=0x0b;
    transfers=fail_at=0;corrupt_time=false;
}
void transfer_args(i2c_master_dev_handle_t handle,int timeout) {
    CHECK(handle==&device_active && bus_active && device_active);
    CHECK(timeout>0 && timeout<=100);
    ++transfers;
}
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config,i2c_master_bus_handle_t *out) {
    CHECK(!bus_active);CHECK(config->sda_io_num==18 && config->scl_io_num==17);
    CHECK(config->flags.enable_internal_pullup);bus_active=true;*out=&bus_active;return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t handle) {
    CHECK(handle==&bus_active && bus_active && !device_active);bus_active=false;return ESP_OK;
}
esp_err_t i2c_master_probe(i2c_master_bus_handle_t handle,uint16_t address,int timeout) {
    CHECK(handle==&bus_active && bus_active && address==0x68 && timeout==100);return attached?ESP_OK:ESP_FAIL;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t handle,const i2c_device_config_t *config,i2c_master_dev_handle_t *out) {
    CHECK(handle==&bus_active && bus_active && !device_active);
    CHECK(config->device_address==0x68 && config->scl_speed_hz==100000);
    device_active=true;*out=&device_active;return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle) {
    CHECK(handle==&device_active && device_active);device_active=false;return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t handle,const uint8_t *reg,size_t write_size,uint8_t *out,size_t read_size,int timeout) {
    transfer_args(handle,timeout);CHECK(write_size==1 && *reg+read_size<=sizeof(chip));
    if (!attached || transfers==fail_at) return ESP_FAIL;
    std::memcpy(out,chip+*reg,read_size);return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t handle,const uint8_t *data,size_t size,int timeout) {
    transfer_args(handle,timeout);CHECK(size>=2 && data[0]+size-1<=sizeof(chip));
    if (!attached || transfers==fail_at) return ESP_FAIL;
    if (data[0]==0x0f) {
        CHECK(size==2);
        // OSF/alarm flags can only be cleared. BSY is read-only.
        chip[0x0f]=(chip[0x0f]&data[1]&0x83)|(data[1]&0x08)|(chip[0x0f]&0x04);
    } else std::memcpy(chip+data[0],data+1,size-1);
    if (data[0]==0 && corrupt_time) chip[0]=0x6a;
    return ESP_OK;
}
static void codec_tests() {
    uint8_t data[]={0x56,0x34,0x12,5,0x24,0x09,0x26};time_t utc{};
    CHECK(ds3231_decode_time(data,0,utc) && utc==SAMPLE);
    CHECK(!ds3231_decode_time(data,0x80,utc));
    data[2]=0x52;CHECK(ds3231_decode_time(data,0,utc) && utc==SAMPLE-12*3600); // 12 AM
    data[2]=0x72;CHECK(ds3231_decode_time(data,0,utc) && utc==SAMPLE); // 12 PM
    data[2]=0x61;CHECK(ds3231_decode_time(data,0,utc) && utc==SAMPLE+3600); // 1 PM
    data[2]=0x40;CHECK(!ds3231_decode_time(data,0,utc));
    const uint8_t invalid[]={0x6a,0x60,0x24,0,0x31,0x89,0xfa};
    for (int i=0;i<7;++i) {
        CHECK(ds3231_encode_time(SAMPLE,data));data[i]=invalid[i];utc=123;
        CHECK(!ds3231_decode_time(data,0,utc) && utc==123);
    }
    CHECK(ds3231_encode_time(951782400,data)); // Leap day 2000-02-29
    CHECK(data[4]==0x29 && data[5]==2 && data[6]==0);
    data[6]=1;CHECK(!ds3231_decode_time(data,0,utc));
    CHECK(ds3231_encode_time(2147483648LL,data)); // Beyond the 2038 signed-32-bit boundary
    CHECK(ds3231_decode_time(data,0,utc) && utc==2147483648LL);
    CHECK(!ds3231_encode_time(946684799,data));CHECK(!ds3231_encode_time(4102444800LL,data));
    // Every day in the supported century, including all leap/month/year boundaries.
    for (int64_t value=946684800;value<4102444800LL;value+=86400) {
        CHECK(ds3231_encode_time(time_t(value+86399),data));
        CHECK(ds3231_decode_time(data,0,utc) && int64_t(utc)==value+86399);
    }
}
static void driver_tests() {
    attached=false;
    { Ds3231 rtc;CHECK(rtc.begin()!=ESP_OK);CHECK(!bus_active && !device_active);
      time_t utc=123;CHECK(rtc.read_utc(utc)!=ESP_OK && utc==123);CHECK(rtc.write_utc(SAMPLE)!=ESP_OK); }
    attached=true;reset_chip();
    {
        Ds3231 rtc;CHECK(rtc.begin()==ESP_OK);time_t utc{};
        CHECK(rtc.read_utc(utc)==ESP_OK && utc==SAMPLE);
        chip[0x0f]|=0x80;CHECK(rtc.read_utc(utc)!=ESP_OK);
        chip[0x0e]|=0x80;
        CHECK(rtc.write_utc(SAMPLE+60)==ESP_OK);
        CHECK(chip[0x0f]==0x0b && chip[0x0e]==0x1c);
        CHECK(rtc.read_utc(utc)==ESP_OK && utc==SAMPLE+60);
        chip[0x0e]|=0x80;CHECK(rtc.read_utc(utc)!=ESP_OK);
        reset_chip();const int before=transfers;
        CHECK(rtc.write_utc(0)!=ESP_OK && transfers==before);
        // Inject a bus failure at every stage; no failure may report a saved time.
        for (int step=1;step<=7;++step) {
            reset_chip();chip[0x0e]|=0x80;chip[0x0f]|=0x80;fail_at=step;
            CHECK(rtc.write_utc(SAMPLE)!=ESP_OK);
            if (step<=6) CHECK(chip[0x0f]&0x80);
        }
        reset_chip();chip[0x0f]|=0x80;corrupt_time=true;CHECK(rtc.write_utc(SAMPLE)!=ESP_OK);
        CHECK(chip[0x0f]&0x80);
        reset_chip();attached=false;CHECK(rtc.read_utc(utc)!=ESP_OK);CHECK(rtc.write_utc(SAMPLE)!=ESP_OK);
        attached=true;
    }
    CHECK(!bus_active && !device_active);
}
int main() {
    codec_tests();driver_tests();
    std::printf("PASS: %u checks (DS3231 UTC/BCD, 12-hour mode, leap dates, OSF, detection, bounded I2C, verified writes, bus failures)\n",checks);
}
