#include "ui_animation.h"
#include <algorithm>
#include <cmath>
float BootAnimation::update(int64_t now,SystemState state,uint32_t held_ms) {
    // Errors and deliberate long presses must remain visible during startup.
    button_wake_=!finished_ && held_ms>=500 && state!=SYSTEM_ERROR;
    if (state==SYSTEM_ERROR || held_ms>=500 || now-started_us_>=DURATION_US) finished_=true;
    if (finished_) return 1.f;
    return std::clamp(float(now-started_us_)/float(DURATION_US),0.f,1.f);
}
void UiAnimation::update(const SalaryStatus &s,int64_t now,float dt) {
    if (!std::isfinite(dt)) dt=0;
    dt=std::clamp(dt,0.f,.05f);
    if (!initialized_ || s.date_key!=date_) {
        initialized_=true;
        date_=s.date_key; physics_.reset(); previous_=WORK_STATE_NO_TIME;
        last_earned_=s.earned_money;
        pulse_=0; gain_money_=0; gain_progress_=1.f;
        transition_state_=WORK_STATE_NO_TIME; transition_progress_=1.f;
    }
    const auto state=s.work_state;
    const bool working=state==WORK_STATE_WORKING_MORNING || state==WORK_STATE_WORKING_AFTERNOON;
    if (state!=previous_) {
        transition_state_=WORK_STATE_NO_TIME; transition_progress_=1.f;
        // Holidays also announce once on the first synced snapshot and each new
        // holiday date. Working periods only announce actual schedule boundaries.
        if (state==WORK_STATE_DAY_OFF ||
            (previous_==WORK_STATE_BEFORE_WORK && state==WORK_STATE_WORKING_MORNING) ||
            (previous_==WORK_STATE_WORKING_MORNING && state==WORK_STATE_LUNCH) ||
            (previous_==WORK_STATE_LUNCH && state==WORK_STATE_WORKING_AFTERNOON) ||
            (previous_==WORK_STATE_WORKING_AFTERNOON && state==WORK_STATE_AFTER_WORK)) {
            transition_state_=state; transition_started_=now; transition_progress_=0;
        }
        if (state==WORK_STATE_DAY_OFF || state==WORK_STATE_BEFORE_WORK) physics_.rest_coin();
        if (working && previous_==WORK_STATE_NO_TIME) {
            physics_.spawn(); physics_.spawn(); last_spawn_=now;
        }
        previous_=state;
    }
    if (transition_state_!=WORK_STATE_NO_TIME) {
        transition_progress_=std::clamp(float(now-transition_started_)/3200000.f,0.f,1.f);
        if (transition_progress_>=1.f) transition_state_=WORK_STATE_NO_TIME;
    }
    // Match the visible two-decimal total, accumulating sub-cent increases until
    // the displayed amount changes. Never show +0.00 or a boot-time windfall.
    const double cents=std::round(s.earned_money*100)-std::round(last_earned_*100);
    if (cents>0) {
        pulse_=1.f; gain_money_=cents/100; gain_started_=now;
    } else pulse_=std::max(0.f,pulse_-dt*4.f);
    if (s.earned_money<last_earned_) {
        pulse_=0; gain_money_=0; gain_progress_=1.f;
    }
    if (gain_money_>0) {
        // Use elapsed time so a delayed frame cannot leave stale text onscreen.
        gain_progress_=std::clamp(float(now-gain_started_)/900000.f,0.f,1.f);
        if (gain_progress_>=1.f) gain_money_=0;
    }
    if (working && std::floor(s.earned_money)>std::floor(last_earned_) && now-last_spawn_>=2500000) {
        physics_.spawn(); last_spawn_=now;
    }
    last_earned_=s.earned_money;
    physics_.update(dt);
    // Preserve the pile while meal/rest scenes are shown, including across lunch.
}
