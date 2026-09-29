#pragma once
#include <array>
#include <cstdint>
// Eight close-packed rows alternate seven/six coins inside the 108 x 109 area.
constexpr unsigned MAX_COINS = 52;
struct Coin {
    float x{}, y{}, vx{}, vy{}, rotation{}, angularVelocity{}, radius{};
    float restitution{}, friction{}, squash{}, age{};
    float quietTime{};
    bool active{}, sleeping{};
    uint64_t order{};
};
class CoinPhysicsEngine {
public:
    explicit CoinPhysicsEngine(uint32_t seed=1) : seed_(seed ? seed : 1) {}
    void reset();
    void spawn();
    // Restore earned coins immediately; optionally let only the newest one fall.
    void set_count(unsigned count,bool animate_last=false);
    void rest_coin();
    void update(float dt);
    const std::array<Coin,MAX_COINS> &coins() const { return coins_; }
    static constexpr float LEFT=204, RIGHT=312, TOP=30, FLOOR=139;
    static constexpr float MIN_RADIUS=7.5f, MAX_RADIUS=7.5f;
private:
    float random(float min,float max);
    void supported(std::array<bool,MAX_COINS> &result) const;
    void substep(float h);
    std::array<Coin,MAX_COINS> coins_{};
    uint32_t seed_;
    uint64_t order_{};
};
