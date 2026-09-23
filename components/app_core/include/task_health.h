#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

enum class CriticalTask : unsigned { Display, Button, Salary, Time, Wifi, Count };
class TaskHealth {
public:
    void beat(CriticalTask task,int64_t now) { times_[static_cast<size_t>(task)]=now; }
    bool healthy(int64_t now,int64_t max_age) const {
        for (const auto time:times_) if (time<=0 || time>now || now-time>max_age) return false;
        return true;
    }
private:
    std::array<int64_t,static_cast<size_t>(CriticalTask::Count)> times_{};
};

enum class BootDecision { Waiting, Valid, Rollback };
class BootProbation {
public:
    BootProbation(int64_t start,int64_t stable,int64_t deadline):start_(start),stable_(stable),deadline_(deadline){}
    BootDecision update(int64_t now,bool healthy,bool fatal) {
        if (fatal || now<start_) return BootDecision::Rollback;
        if (!healthy) healthy_since_=-1;
        else if (healthy_since_<0) healthy_since_=now;
        if (healthy_since_>=0 && now-healthy_since_>=stable_) return BootDecision::Valid;
        return now-start_>=deadline_ ? BootDecision::Rollback : BootDecision::Waiting;
    }
private:
    int64_t start_,stable_,deadline_,healthy_since_{-1};
};
