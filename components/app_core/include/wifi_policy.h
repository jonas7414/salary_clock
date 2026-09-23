#pragma once
#include <cstdint>
enum class WifiLinkState { Disconnected, Connecting, WaitingForIp, Online };
enum class WifiAction { None, Connect, Disconnect, Setup };
class WifiPolicy {
public:
    static constexpr int64_t CONNECT_TIMEOUT_US=20000000, DHCP_TIMEOUT_US=60000000, RETRY_US=5000000;
    explicit WifiPolicy(bool configured,int64_t now_us):configured_(configured),boot_at_(now_us),state_at_(now_us),retry_at_(now_us){}
    WifiAction update(WifiLinkState state,bool setup,int64_t now_us) {
        if (setup) return WifiAction::None;
        if (!configured_) return WifiAction::Setup;
        if (state!=state_) {
            state_=state; state_at_=now_us; recovery_requested_=false;
            if (state==WifiLinkState::Disconnected) retry_at_=now_us+RETRY_US;
        }
        if (state==WifiLinkState::Online) { connected_once_=true; return WifiAction::None; }
        if (state==WifiLinkState::WaitingForIp) {
            // Association succeeded: DHCP gets its own deadline, starting here.
            if (now_us-state_at_<DHCP_TIMEOUT_US || recovery_requested_) return WifiAction::None;
            recovery_requested_=true;
            return connected_once_ ? WifiAction::Disconnect : WifiAction::Setup;
        }
        if (!connected_once_ && now_us-boot_at_>=CONNECT_TIMEOUT_US) return WifiAction::Setup;
        if (state==WifiLinkState::Connecting) {
            // Do not restart a driver scan/auth attempt every five seconds.
            if (now_us-state_at_>=CONNECT_TIMEOUT_US && !recovery_requested_) {
                recovery_requested_=true; return WifiAction::Disconnect;
            }
            return WifiAction::None;
        }
        if (now_us>=retry_at_) { retry_at_=now_us+RETRY_US; return WifiAction::Connect; }
        return WifiAction::None;
    }
private:
    bool configured_;
    bool connected_once_{};
    bool recovery_requested_{};
    WifiLinkState state_=WifiLinkState::Disconnected;
    int64_t boot_at_,state_at_,retry_at_;
};
