#include "ds3231.h"
#include "ds3231_time.h"
namespace {
constexpr uint8_t ADDRESS=0x68, CONTROL=0x0e, STATUS=0x0f, OSF=0x80, EOSC=0x80;
constexpr int TIMEOUT_MS=100;
}
Ds3231::~Ds3231() {
    if (device_) i2c_master_bus_rm_device(device_);
    if (bus_) i2c_del_master_bus(bus_);
}
esp_err_t Ds3231::begin() {
    i2c_master_bus_config_t bus{};
    bus.i2c_port=I2C_NUM_0;bus.sda_io_num=GPIO_NUM_43;bus.scl_io_num=GPIO_NUM_44;
    bus.clk_source=I2C_CLK_SRC_DEFAULT;bus.glitch_ignore_cnt=7;
    bus.flags.enable_internal_pullup=true;
    auto err=i2c_new_master_bus(&bus,&bus_);
    if (err!=ESP_OK) return err;
    err=i2c_master_probe(bus_,ADDRESS,TIMEOUT_MS);
    if (err==ESP_OK) {
        i2c_device_config_t config{};
        config.dev_addr_length=I2C_ADDR_BIT_LEN_7;config.device_address=ADDRESS;config.scl_speed_hz=100000;
        err=i2c_master_bus_add_device(bus_,&config,&device_);
    }
    if (err!=ESP_OK) { i2c_del_master_bus(bus_);bus_=nullptr; }
    return err;
}
esp_err_t Ds3231::read(uint8_t reg,uint8_t *data,size_t size) {
    if (!device_) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(device_,&reg,1,data,size,TIMEOUT_MS);
}
esp_err_t Ds3231::write_register(uint8_t reg,uint8_t value) {
    const uint8_t data[]={reg,value};
    return i2c_master_transmit(device_,data,sizeof(data),TIMEOUT_MS);
}
esp_err_t Ds3231::read_utc(time_t &utc) {
    // One burst latches a coherent date/time, including across midnight.
    uint8_t data[16]{};
    const auto err=read(0,data,sizeof(data));
    if (err!=ESP_OK) return err;
    if ((data[CONTROL]&EOSC) || !ds3231_decode_time(data,data[STATUS],utc)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
esp_err_t Ds3231::write_utc(time_t utc) {
    if (!device_) return ESP_ERR_INVALID_STATE;
    uint8_t data[8]{}; // Register pointer followed by the seven time registers.
    if (!ds3231_encode_time(utc,data+1)) return ESP_ERR_INVALID_ARG;
    uint8_t control=0,status=0;
    auto err=read(CONTROL,&control,1);
    // Keep the oscillator running on the backup battery, preserving alarm setup.
    if (err==ESP_OK && (control&EOSC)) err=write_register(CONTROL,control&~EOSC);
    if (err==ESP_OK) err=i2c_master_transmit(device_,data,sizeof(data),TIMEOUT_MS);
    // Verify the date before clearing OSF, so a bad write cannot become trusted on reboot.
    uint8_t saved[7]{};time_t verified{};
    if (err==ESP_OK) err=read(0,saved,sizeof(saved));
    if (err==ESP_OK && (!ds3231_decode_time(saved,0,verified) || verified<utc || verified-utc>1)) err=ESP_FAIL;
    if (err==ESP_OK) err=read(STATUS,&status,1);
    // OSF is only cleared after a successful time write; writing 1 preserves alarm flags.
    if (err==ESP_OK) err=write_register(STATUS,(status&~OSF)|0x03);
    if (err!=ESP_OK) return err;
    err=read_utc(verified);
    if (err!=ESP_OK) return err;
    return verified>=utc && verified-utc<=1 ? ESP_OK : ESP_FAIL;
}
