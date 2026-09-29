#pragma once
#include <array>
#include <cstdint>
// One workday's coins, with room for an irregular pile inside the 108 x 109 area.
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
    // Restore a random supported pile; a single increment preserves it and drops a coin.
    void set_count(unsigned count,bool animate_last=false);
    void rest_coin();
    void update(float dt);
    const std::array<Coin,MAX_COINS> &coins() const { return coins_; }
    static constexpr float LEFT=204, RIGHT=312, TOP=30, FLOOR=139;
    static constexpr float MIN_RADIUS=6.f, MAX_RADIUS=7.f;
private:
    float random(float min,float max);
    void supported(std::array<bool,MAX_COINS> &result) const;
    void substep(float h);
    std::array<Coin,MAX_COINS> coins_{};
    uint32_t seed_;
    uint64_t order_{};
};
