#include "app_types.h"
#include <cstring>

AppConfig config_defaults() {
    AppConfig c{};
    c.version = CONFIG_VERSION;
    c.monthly_salary = 40000;
    c.work_days = 0x1f;
    c.work_start = 9 * 60;
    c.lunch_start = 12 * 60;
    c.lunch_end = 13 * 60;
    c.work_end = 18 * 60;
    std::strcpy(c.timezone, "Asia/Taipei");
    return c;
}
const char *timezone_posix(const char *name) {
    struct Zone { const char *iana; const char *posix; };
    static const Zone zones[] = {{"Asia/Taipei", "CST-8"}, {"Asia/Tokyo", "JST-9"},
        {"Asia/Hong_Kong", "HKT-8"}, {"Asia/Singapore", "SGT-8"}, {"UTC", "UTC0"}};
    for (const auto &zone : zones) if (std::strcmp(name, zone.iana) == 0) return zone.posix;
    return nullptr;
}
bool config_validate(const AppConfig &c, bool require_ssid, const char **reason) {
    const char *error = nullptr;
    if (c.version != CONFIG_VERSION) error = "Unsupported configuration version";
    else if (!std::memchr(c.wifi_ssid, 0, sizeof(c.wifi_ssid)) ||
             !std::memchr(c.wifi_password, 0, sizeof(c.wifi_password)) ||
             !std::memchr(c.timezone, 0, sizeof(c.timezone))) error = "Invalid string length";
    else if (require_ssid && !c.wifi_ssid[0]) error = "SSID is required";
    else if (!c.monthly_salary || c.monthly_salary > 1000000000U) error = "Salary must be 1..1000000000";
    else if (!c.work_days || (c.work_days & 0x80)) error = "Select at least one work day";
    else if (!(c.work_start < c.lunch_start && c.lunch_start < c.lunch_end &&
               c.lunch_end < c.work_end && c.work_end < 1440)) error = "Require start < lunch start < lunch end < end";
    else if (!timezone_posix(c.timezone)) error = "Unsupported timezone";
    else {
        const size_t n = std::strlen(c.wifi_password);
        if (n > 0 && n < 8) error = "Password must be empty or 8..63 characters (or 64 hex digits)";
        if (n == 64) for (size_t i = 0; i < n; ++i) {
            const char ch = c.wifi_password[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
                error = "A 64-character PSK must be hexadecimal"; break;
            }
        }
    }
    if (reason) *reason = error;
    return !error;
}
uint32_t hm_to_seconds(uint8_t h, uint8_t m) { return uint32_t(h) * 3600U + uint32_t(m) * 60U; }
uint32_t config_checksum(const AppConfig &c) {
    // CRC covers every stored byte, including the version and credential fields.
    uint32_t crc = 0xffffffffU;
    const auto *p = reinterpret_cast<const uint8_t *>(&c);
    for (size_t i = 0; i < sizeof(c); ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
