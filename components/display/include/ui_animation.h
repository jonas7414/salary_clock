#pragma once
#include "app_types.h"
#include "coin_physics.h"
class UiAnimation {
public:
    explicit UiAnimation(uint32_t seed=1):physics_(seed){}
    void update(const SalaryStatus &salary,int64_t now_us,float dt);
    const CoinPhysicsEngine &physics() const { return physics_; }
    float pulse() const { return pulse_; }
private:
    CoinPhysicsEngine physics_;
    WorkState previous_{WORK_STATE_NO_TIME};
    int date_{};
    double last_earned_{};
    int64_t last_spawn_{};
    float pulse_{};
};
