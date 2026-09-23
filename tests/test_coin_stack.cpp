#include "coin_physics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks=0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr,"CHECK failed, line %d: %s\n",__LINE__,#condition); std::exit(1); } } while (false)
static auto &fixture(CoinPhysicsEngine &engine) {
    // Only the tests mutate fixtures; the product exposes read-only coin data.
    return const_cast<std::array<Coin,MAX_COINS>&>(engine.coins());
}
static void advance(CoinPhysicsEngine &engine,float seconds,float dt=.04f) {
    for (unsigned frame=0;frame<static_cast<unsigned>(seconds/dt);++frame) engine.update(dt);
}
static void check_geometry(const CoinPhysicsEngine &engine,float tolerance) {
    const auto &coins=engine.coins();
    for (unsigned i=0;i<MAX_COINS;++i) {
        const auto &a=coins[i]; if (!a.active) continue;
        CHECK(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.vx) && std::isfinite(a.vy));
        CHECK(a.radius >= CoinPhysicsEngine::MIN_RADIUS && a.radius <= CoinPhysicsEngine::MAX_RADIUS);
        CHECK(a.x-a.radius >= CoinPhysicsEngine::LEFT-tolerance);
        CHECK(a.x+a.radius <= CoinPhysicsEngine::RIGHT+tolerance);
        CHECK(a.y+a.radius <= CoinPhysicsEngine::FLOOR+tolerance);
        for (unsigned j=i+1;j<MAX_COINS;++j) {
            const auto &b=coins[j]; if (!b.active) continue;
            const float dx=b.x-a.x,dy=b.y-a.y;
            CHECK(std::sqrt(dx*dx+dy*dy) >= a.radius+b.radius-tolerance);
        }
    }
}
static Coin resting(float x,float y,float radius=10) {
    Coin coin{}; coin.active=true; coin.sleeping=true;
    coin.x=x; coin.y=y; coin.radius=radius; coin.restitution=.4f; coin.friction=.6f;
    return coin;
}
int main() {
    CoinPhysicsEngine boundaries(42);
    for (unsigned i=0;i<MAX_COINS+7;++i) boundaries.spawn();
    for (unsigned frame=0;frame<2000;++frame) {
        boundaries.update(frame==10 ? 8.f : .04f);
        for (const auto &coin:boundaries.coins()) {
            CHECK(coin.x-coin.radius >= CoinPhysicsEngine::LEFT-.01f);
            CHECK(coin.x+coin.radius <= CoinPhysicsEngine::RIGHT+.01f);
            CHECK(coin.y+coin.radius <= CoinPhysicsEngine::FLOOR+.01f);
        }
    }
    for (uint32_t seed=1;seed<=8;++seed) {
        CoinPhysicsEngine engine(seed);
        for (unsigned i=0;i<MAX_COINS;++i) engine.spawn();
        check_geometry(engine,0);
        for (unsigned frame=0;frame<500;++frame) {
            engine.update(.04f);
            check_geometry(engine,.55f);
        }
        unsigned elevated=0;
        for (const auto &coin:engine.coins()) {
            if (!coin.sleeping) std::fprintf(stderr,"seed %u awake: x %.2f y %.2f v %.2f %.2f quiet %.2f\n",seed,coin.x,coin.y,coin.vx,coin.vy,coin.quietTime);
            CHECK(coin.sleeping && coin.vx==0 && coin.vy==0);
            if (coin.y+coin.radius < CoinPhysicsEngine::FLOOR-5) ++elevated;
        }
        CHECK(elevated>=8);
        check_geometry(engine,.2f);
        const auto settled=engine.coins();
        advance(engine,2);
        for (unsigned i=0;i<MAX_COINS;++i) {
            CHECK(engine.coins()[i].x==settled[i].x && engine.coins()[i].y==settled[i].y);
        }
        unsigned top=0;
        for (unsigned i=1;i<MAX_COINS;++i)
            if (settled[i].y<settled[top].y) top=i;
        engine.spawn();
        for (unsigned i=0;i<MAX_COINS;++i) if (i!=top)
            CHECK(engine.coins()[i].order==settled[i].order);
        CHECK(engine.coins()[top].order>settled[top].order);
        advance(engine,8);
        check_geometry(engine,.2f);
    }
    // Removing support wakes all coins above it, including an entire column.
    CoinPhysicsEngine removed(4);
    fixture(removed)[0]=resting(250,129);
    fixture(removed)[1]=resting(250,109);
    fixture(removed)[2]=resting(250,89);
    fixture(removed)[0].active=false;
    removed.update(.04f);
    CHECK(!removed.coins()[1].sleeping && !removed.coins()[2].sleeping);
    CHECK(removed.coins()[1].y>109 && removed.coins()[2].y>89);
    advance(removed,4);
    CHECK(removed.coins()[1].sleeping && removed.coins()[2].sleeping);
    check_geometry(removed,.2f);

    // A hard impact wakes its sleeping target and transfers lateral momentum.
    CoinPhysicsEngine hit(9);
    fixture(hit)[0]=resting(260,129);
    fixture(hit)[1]=resting(236,125);
    fixture(hit)[1].sleeping=false; fixture(hit)[1].vx=180; fixture(hit)[1].vy=20;
    bool woke=false,moved=false;
    for (unsigned frame=0;frame<40;++frame) {
        hit.update(.01f);
        woke|=!hit.coins()[0].sleeping;
        moved|=std::abs(hit.coins()[0].x-260)>.3f;
        check_geometry(hit,.3f);
    }
    if (!woke || !moved) std::fprintf(stderr,"impact woke=%d moved=%d target x=%.3f\n",woke,moved,hit.coins()[0].x);
    CHECK(woke && moved);
    advance(hit,6); CHECK(hit.coins()[0].sleeping && hit.coins()[1].sleeping);

    // Frame-rate independence uses equal 1/240s substeps at 20 and 30 FPS.
    CoinPhysicsEngine fast(55),slow(55);
    for (unsigned i=0;i<8;++i) { fast.spawn(); slow.spawn(); }
    for (unsigned frame=0;frame<90;++frame) fast.update(1.f/30);
    for (unsigned frame=0;frame<60;++frame) slow.update(.05f);
    for (unsigned i=0;i<8;++i) {
        CHECK(std::abs(fast.coins()[i].x-slow.coins()[i].x)<.2f);
        CHECK(std::abs(fast.coins()[i].y-slow.coins()[i].y)<.2f);
    }
    CoinPhysicsEngine clamp(8),reference(8); clamp.spawn(); reference.spawn();
    clamp.update(10); reference.update(.05f);
    CHECK(clamp.coins()[0].x==reference.coins()[0].x && clamp.coins()[0].y==reference.coins()[0].y);
    const auto before=clamp.coins()[0];
    clamp.update(-1); clamp.update(0); clamp.update(std::numeric_limits<float>::quiet_NaN());
    clamp.update(std::numeric_limits<float>::infinity());
    CHECK(clamp.coins()[0].x==before.x && clamp.coins()[0].y==before.y);

    // Repeated capacity replacement and impacts keep a bounded, finite pile.
    CoinPhysicsEngine stress(12345);
    for (unsigned frame=0;frame<1000;++frame) {
        if (frame%8==0) stress.spawn();
        stress.update(.04f);
        check_geometry(stress,.55f);
    }
    advance(stress,15);
    check_geometry(stress,.2f);
    for (const auto &coin:stress.coins()) CHECK(coin.sleeping);
    std::printf("PASS: %u coin stacking checks\n",checks);
}
