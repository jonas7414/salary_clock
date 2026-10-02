#pragma once
#include <cstdint>

enum class CrashReportState { Hidden, Waiting, Prompt, Sending, Sent, Failed, CleanupFailed };
struct CrashReportStatus {
    CrashReportState state{CrashReportState::Hidden};
    bool report{}; // Default to Later.
    int http_status{};
};
inline bool crash_report_visible(const CrashReportStatus &s) {
    return s.state!=CrashReportState::Hidden && s.state!=CrashReportState::Waiting;
}

// Owned by one worker. UI receives a snapshot and posts commands to that worker.
class CrashReportPolicy {
public:
    CrashReportStatus status{};
    void begin(int64_t now) { status={CrashReportState::Waiting,false,0}; deadline_=now+90000000; }
    void poll(int64_t now,bool network_ready,bool ui_ready) {
        if (status.state==CrashReportState::Waiting) {
            if (now>=deadline_) status.state=CrashReportState::Hidden;
            else if (network_ready && ui_ready) status.state=CrashReportState::Prompt;
        } else if (status.state==CrashReportState::Prompt && !network_ready) {
            status.state=CrashReportState::Hidden;
        }
    }
    void next() { if (status.state==CrashReportState::Prompt) status.report=!status.report; }
    void confirm(bool network_ready) {
        if (status.state==CrashReportState::Prompt)
            status.state=status.report && network_ready?CrashReportState::Sending:CrashReportState::Hidden;
        else if (status.state==CrashReportState::Sent || status.state==CrashReportState::Failed ||
                 status.state==CrashReportState::CleanupFailed) status.state=CrashReportState::Hidden;
    }
    void complete(bool uploaded,bool erased,int http_status) {
        status.http_status=http_status;
        status.state=!uploaded?CrashReportState::Failed:erased?CrashReportState::Sent:CrashReportState::CleanupFailed;
    }
private:
    int64_t deadline_{};
};

// Only a complete Loki 204 acknowledgment authorizes deletion.
inline bool crash_report_acknowledged(bool headers_received,int http_status) {
    return headers_received && http_status==204;
}
template<class Upload,class Erase>
CrashReportState crash_report_deliver(Upload upload,Erase erase) {
    if (!upload()) return CrashReportState::Failed;
    return erase()?CrashReportState::Sent:CrashReportState::CleanupFailed;
}
