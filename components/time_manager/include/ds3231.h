#pragma once
#include "driver/i2c_master.h"
#include <ctime>

// Owned exclusively by the time task. All transfers have a bounded timeout.
class Ds3231 {
public:
    ~Ds3231();
    esp_err_t begin();
    esp_err_t read_utc(time_t &utc);
    esp_err_t write_utc(time_t utc);
private:
    i2c_master_bus_handle_t bus_{};
    i2c_master_dev_handle_t device_{};
    esp_err_t read(uint8_t reg,uint8_t *data,size_t size);
    esp_err_t write_register(uint8_t reg,uint8_t value);
};
