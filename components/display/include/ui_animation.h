#pragma once
#include "app_types.h"
#include "coin_physics.h"
class UiAnimation {
public:
    explicit UiAnimation(uint32_t seed=1):physics_(seed){}
    void update(const SalaryStatus &salary,int64_t now_us,float dt);
    const CoinPhysicsEngine &physics() const { return physics_; }
    float pulse() const { return pulse_; }
    double gain_money() const { return gain_money_; }
    float gain_progress() const { return gain_progress_; }
    WorkState transition_state() const { return transition_state_; }
    float transition_progress() const { return transition_progress_; }
private:
    CoinPhysicsEngine physics_;
    WorkState previous_{WORK_STATE_NO_TIME};
    int date_{};
    double last_earned_{};
    int64_t last_spawn_{};
    float pulse_{};
    bool initialized_{};
    double gain_money_{};
    int64_t gain_started_{};
    float gain_progress_{1.f};
    WorkState transition_state_{WORK_STATE_NO_TIME};
    int64_t transition_started_{};
    float transition_progress_{1.f};
};
