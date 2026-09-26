#pragma once
#include <cstdint>
#include <cstring>
enum class OtaState { IDLE, CHECKING, UPDATE_AVAILABLE, DOWNLOADING, VERIFYING, READY_TO_REBOOT, ERROR };
struct OtaStatus {
    OtaState state{OtaState::IDLE};
    char latest_version[32]{};
    uint32_t downloaded_bytes{},total_bytes{};
    unsigned percentage{};
    int http_status{};
    int32_t error{};
    char message[96]{};
    bool checked{},current{},prompt{},foreground{},busy{};
    unsigned choice{1}; // Default to Later; never accept an update accidentally.
};
inline bool ota_version_approved(const char *approved,const char *latest) {
    return approved && approved[0] && std::strcmp(approved,latest)==0;
}
inline bool ota_should_prompt(bool manual,const char *latest,const char *ignored) {
    return manual || std::strcmp(latest,ignored)!=0;
}
inline bool ota_request_allowed(const OtaStatus &status,bool needs_prompt) {
    return !status.busy && (!needs_prompt || (status.prompt && status.state==OtaState::UPDATE_AVAILABLE));
}
class BootUpdateCheck {
public:
    explicit BootUpdateCheck(int64_t delay_us):delay_(delay_us){}
    bool due(int64_t now,bool ready) {
        if (!armed_ && ready) { armed_=true; deadline_=now+delay_; }
        return !checked_ && armed_ && ready && now>=deadline_;
    }
    void complete() { checked_=true; }
    bool checked() const { return checked_; }
private:
    int64_t delay_{},deadline_{};
    bool armed_{},checked_{};
};
