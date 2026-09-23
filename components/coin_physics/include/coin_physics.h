#pragma once
#include <array>
#include <cstdint>
constexpr unsigned MAX_COINS = 16;
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
    void rest_coin();
    void update(float dt);
    const std::array<Coin,MAX_COINS> &coins() const { return coins_; }
    static constexpr float LEFT=204, RIGHT=312, TOP=30, FLOOR=139;
    static constexpr float MIN_RADIUS=9, MAX_RADIUS=12;
private:
    float random(float min,float max);
    void supported(std::array<bool,MAX_COINS> &result) const;
    void substep(float h);
    std::array<Coin,MAX_COINS> coins_{};
    uint32_t seed_;
    uint64_t order_{};
};
