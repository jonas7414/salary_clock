#include "battery_status.h"
const char *battery_state_name(BatteryState state) {
    switch (state) {
        case BatteryState::Measuring:return "measuring";
        case BatteryState::BatteryPower:return "battery_power";
        case BatteryState::ExternalPower:return "external_power";
        case BatteryState::Unknown:return "unknown";
        default:return "unavailable";
    }
}
BatteryStatus BatteryMonitor::update(int mv) {
    if (mv<2500 || mv>5500) {
        consecutive_=0;candidate_=BatteryState::Unknown;
        return {BatteryState::Unavailable,0};
    }
    // Official example uses 4.3 V. Leave a +/-100 mV uncertainty band for ADC,
    // resistor tolerance and power transitions instead of oscillating the label.
    const auto detected=mv<=4200?BatteryState::BatteryPower:
        mv>=4400?BatteryState::ExternalPower:BatteryState::Unknown;
    if (detected!=candidate_) { candidate_=detected;consecutive_=0; }
    if (consecutive_<3) ++consecutive_;
    return {consecutive_>=3?detected:BatteryState::Measuring,static_cast<uint16_t>(mv)};
}
