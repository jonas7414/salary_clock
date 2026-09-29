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
static Coin resting(float x,float y,float radius=CoinPhysicsEngine::MAX_RADIUS) {
    Coin coin{}; coin.active=true; coin.sleeping=true;
    coin.x=x; coin.y=y; coin.radius=radius; coin.restitution=.4f; coin.friction=.6f;
    return coin;
}
int main() {
    // Every restored milestone fits inside the visible area without overlap.
    CoinPhysicsEngine progress(7);
    for(unsigned count=0;count<=MAX_COINS;++count) {
        progress.set_count(count);check_geometry(progress,.01f);
        unsigned active=0;
        for(const auto &coin:progress.coins())if(coin.active) {
            ++active;CHECK(coin.sleeping && coin.y-coin.radius>=CoinPhysicsEngine::TOP);
        }
        CHECK(active==count);
    }
    // Restored piles have varied poses, remain supported, and fit at every milestone.
    for(uint32_t seed=1;seed<=32;++seed)for(unsigned count:{1U,13U,26U,39U,MAX_COINS}) {
        CoinPhysicsEngine restored(seed);restored.set_count(count);
        restored.update(.04f);check_geometry(restored,.01f);
        for(unsigned i=0;i<count;++i) {
            const auto &c=restored.coins()[i];
            CHECK(c.sleeping && c.y-c.radius>=CoinPhysicsEngine::TOP);
            if(i)CHECK(c.rotation!=restored.coins()[i-1].rotation);
        }
    }
    CoinPhysicsEngine repeat_a(23),repeat_b(23),different(24);
    repeat_a.set_count(26);repeat_b.set_count(26);different.set_count(26);
    bool varied=false;
    for(unsigned i=0;i<26;++i) {
        CHECK(repeat_a.coins()[i].x==repeat_b.coins()[i].x && repeat_a.coins()[i].y==repeat_b.coins()[i].y);
        varied|=repeat_a.coins()[i].x!=different.coins()[i].x || repeat_a.coins()[i].y!=different.coins()[i].y;
    }
    CHECK(varied);
    // The actual single-increment API must preserve every existing coin, then
    // let the random incoming coin rebound and wake neighbors through collisions.
    CoinPhysicsEngine earned(17);earned.set_count(25);
    const auto before_drop=earned.coins();earned.set_count(26,true);
    for(unsigned i=0;i<25;++i) {
        CHECK(earned.coins()[i].x==before_drop[i].x && earned.coins()[i].y==before_drop[i].y);
        CHECK(earned.coins()[i].rotation==before_drop[i].rotation && earned.coins()[i].order==before_drop[i].order);
    }
    CHECK(!earned.coins()[25].sleeping && std::abs(earned.coins()[25].vx)>0);
    bool bounced=false,woke_neighbors=false;
    for(unsigned frame=0;frame<200;++frame) {
        earned.update(.04f);check_geometry(earned,.55f);
        bounced|=earned.coins()[25].vy<0;
        for(unsigned i=0;i<25;++i)
            woke_neighbors|=!earned.coins()[i].sleeping;
    }
    CHECK(bounced && woke_neighbors);
    // A whole workday uses incremental drops, never the restore layout.
    for(uint32_t seed=1;seed<=32;++seed) {
        CoinPhysicsEngine day(seed);
        for(unsigned count=1;count<=MAX_COINS;++count) {
            day.set_count(count,true);
            bool asleep=false;
            for(unsigned frame=0;frame<200 && !asleep;++frame) {
                day.update(.04f);asleep=true;
                for(const auto &c:day.coins())if(c.active)asleep&=c.sleeping;
            }
            CHECK(asleep);check_geometry(day,.2f);
            for(const auto &c:day.coins())if(c.active) {
                if(c.y-c.radius<CoinPhysicsEngine::TOP)std::fprintf(stderr,"day seed %u count %u top %.2f x %.2f\n",seed,count,c.y-c.radius,c.x);
                CHECK(c.y-c.radius>=CoinPhysicsEngine::TOP);
            }
        }
    }
    // A newly earned coin falls onto small and nearly-full piles.
    for(unsigned count:{1U,7U,8U,13U,14U,26U,39U,MAX_COINS}) {
        progress.set_count(count,true);CHECK(!progress.coins()[count-1].sleeping);
        advance(progress,8);check_geometry(progress,.2f);
        for(const auto &coin:progress.coins())if(coin.active)
            CHECK(coin.sleeping && coin.y-coin.radius>=CoinPhysicsEngine::TOP);
    }

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
    fixture(removed)[0]=resting(250,131.5f);
    fixture(removed)[1]=resting(250,116.5f);
    fixture(removed)[2]=resting(250,101.5f);
    fixture(removed)[0].active=false;
    removed.update(.04f);
    CHECK(!removed.coins()[1].sleeping && !removed.coins()[2].sleeping);
    CHECK(removed.coins()[1].y>116.5f && removed.coins()[2].y>101.5f);
    advance(removed,4);
    CHECK(removed.coins()[1].sleeping && removed.coins()[2].sleeping);
    check_geometry(removed,.2f);

    // A hard impact wakes its sleeping target and transfers lateral momentum.
    CoinPhysicsEngine hit(9);
    fixture(hit)[0]=resting(260,131.5f);
    fixture(hit)[1]=resting(240,127.5f);
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
