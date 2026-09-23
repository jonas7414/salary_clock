#include "ui_animation.h"
#include <algorithm>
#include <cmath>
void UiAnimation::update(const SalaryStatus &s,int64_t now,float dt) {
    if (!std::isfinite(dt)) dt=0;
    dt=std::clamp(dt,0.f,.05f);
    if (s.date_key!=date_) {
        date_=s.date_key; physics_.reset(); previous_=WORK_STATE_NO_TIME;
        last_earned_=s.earned_money;
    }
    const auto state=s.work_state;
    const bool working=state==WORK_STATE_WORKING_MORNING || state==WORK_STATE_WORKING_AFTERNOON;
    if (state!=previous_) {
        if (state==WORK_STATE_DAY_OFF || state==WORK_STATE_BEFORE_WORK) physics_.rest_coin();
        if (working && previous_==WORK_STATE_NO_TIME) {
            physics_.spawn(); physics_.spawn(); last_spawn_=now;
        }
        previous_=state;
    }
    if (s.earned_money>last_earned_) pulse_=1.f;
    else pulse_=std::max(0.f,pulse_-dt*4.f);
    if (working && std::floor(s.earned_money)>std::floor(last_earned_) && now-last_spawn_>=2500000) {
        physics_.spawn(); last_spawn_=now;
    }
    last_earned_=s.earned_money;
    physics_.update(dt);
    // Preserve the pile while meal/rest scenes are shown, including across lunch.
}
