#pragma once
#include <cstdint>
constexpr uint8_t BRIGHTNESS_EDIT=250,BRIGHTNESS_DOWN=251,BRIGHTNESS_UP=252;
constexpr bool brightness_valid(uint32_t value) { return value>=10 && value<=100 && value%10==0; }
constexpr uint32_t brightness_duty(uint32_t value) { return value*1023/100; }
constexpr uint32_t brightness_step(uint32_t value,bool up) {
    return up ? (value<100?value+10:100) : (value>10?value-10:10);
}
