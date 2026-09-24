#pragma once
#include <cstdint>

// The stock T-Display-S3 measures its supply rail, not the cell before power switching.
// External power masks both battery presence and cell voltage; never report "None".
enum class BatteryState : uint8_t { Measuring, BatteryPower, ExternalPower, Unknown, Unavailable };
struct BatteryStatus {
    BatteryState state{BatteryState::Measuring};
    uint16_t supply_mv{}; // Calibrated GPIO4 voltage times the 100K/100K divider ratio.
};
const char *battery_state_name(BatteryState state);
class BatteryMonitor {
public:
    // Three consecutive samples must agree before inferring a power source.
    BatteryStatus update(int supply_mv);
private:
    BatteryState candidate_{BatteryState::Unknown};
    unsigned consecutive_{};
};
