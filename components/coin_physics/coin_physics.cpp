#include "coin_physics.h"
#include <algorithm>
#include <cmath>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
namespace {
constexpr float GRAVITY=560.f;
constexpr float CONTACT_GAP=.15f;
constexpr float SUPPORT_GAP=.6f;
constexpr unsigned SOLVER_PASSES=10;
void wake(Coin &c) { c.sleeping=false; c.quietTime=0; }
}
float CoinPhysicsEngine::random(float lo,float hi) {
    seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
    return lo + (hi-lo) * float(seed_ & 0xffffffU) / float(0xffffffU);
}
void CoinPhysicsEngine::reset() { coins_={}; }
void CoinPhysicsEngine::spawn() {
    Coin *slot=nullptr;
    for (auto &c : coins_) {
        if (!c.active) { slot=&c; break; }
        if (!slot || c.y < slot->y || (c.y == slot->y && c.order < slot->order)) slot=&c;
    }
    // Recycle the highest coin when full, leaving the supporting base intact.
    *slot={};
    slot->radius=random(MIN_RADIUS,MAX_RADIUS);
    slot->x=random(LEFT+slot->radius,RIGHT-slot->radius);
    float pile_top=FLOOR;
    for(const auto &c:coins_)if(c.active && c.sleeping)pile_top=std::min(pile_top,c.y-c.radius);
    const bool crowded=pile_top<TOP+4*MAX_RADIUS;
    if(crowded) {
        // Near capacity, scatter into a lower part of the pile instead of hiding
        // earned coins above the screen. Landing is still resolved by collisions.
        float lowest=-1e9f;
        for(unsigned trial=0;trial<16;++trial) {
            const float x=random(LEFT+slot->radius,RIGHT-slot->radius);
            float y=FLOOR-slot->radius;
            for(const auto &c:coins_)if(c.active && c.sleeping) {
                const float dx=x-c.x,r=slot->radius+c.radius;
                if(std::abs(dx)<r)y=std::min(y,c.y-std::sqrt(r*r-dx*dx));
            }
            if(y>lowest) { lowest=y;slot->x=x; }
        }
    }
    slot->y=TOP-slot->radius-random(0,8);
    // Burst spawns form a separated queue above the clipped display area.
    for (const auto &c:coins_) if (c.active)
        slot->y=std::min(slot->y,c.y-c.radius-slot->radius-2.f);
    slot->vx=crowded?random(-8,8):random(-25,25); slot->vy=random(6,20);
    slot->rotation=random(-3.14f,3.14f);
    slot->angularVelocity=random(3,8) * (random(0,1) > .5f ? 1 : -1);
    slot->restitution=random(.34f,.49f); slot->friction=random(.52f,.67f);
    slot->active=true; slot->order=++order_;
#ifdef ESP_PLATFORM
    ESP_LOGD("physics","Coin spawned");
#endif
}
void CoinPhysicsEngine::rest_coin() {
    reset(); spawn(); auto &c=coins_[0];
    c.x=258; c.y=FLOOR-c.radius; c.vx=c.vy=c.angularVelocity=0;
    c.rotation=.2f; c.sleeping=true;
}
void CoinPhysicsEngine::set_count(unsigned count,bool animate_last) {
    count=std::min(count,MAX_COINS);
    unsigned current=0;
    for(const auto &c:coins_)current+=c.active;
    if(animate_last && count==current+1) { spawn();return; }
    reset();
    const unsigned settled=count-(animate_last && count>0 ? 1U : 0U);
    for(unsigned i=0;i<settled;++i) {
        spawn();auto &c=coins_[i];
        float best=-1e9f;
        // A restart restores contact pockets directly instead of running seconds
        // of simulation in the display task. Random radii and pocket choices keep
        // the pile irregular. Each pocket rests on the floor, wall + coin, or two coins.
        const auto consider=[&](float x,float y) {
            if(x<LEFT+c.radius-.001f || x>RIGHT-c.radius+.001f || y>FLOOR-c.radius+.001f)return;
            for(unsigned j=0;j<i;++j) {
                const auto &b=coins_[j];const float dx=x-b.x,dy=y-b.y,r=c.radius+b.radius;
                if(dx*dx+dy*dy<(r-.001f)*(r-.001f))return;
            }
            const float score=y+random(-2.f,2.f);
            if(score>best) { best=score;c.x=x;c.y=y; }
        };
        consider(LEFT+c.radius,FLOOR-c.radius);
        consider(RIGHT-c.radius,FLOOR-c.radius);
        for(unsigned trial=0;trial<12;++trial)
            consider(random(LEFT+c.radius,RIGHT-c.radius),FLOOR-c.radius);
        for(unsigned j=0;j<i;++j) {
            const auto &a=coins_[j];const float ra=c.radius+a.radius;
            // Floor contacts beside a bottom coin, and contacts against either wall.
            const float dy=FLOOR-c.radius-a.y;
            if(std::abs(dy)<ra) {
                const float dx=std::sqrt(ra*ra-dy*dy);
                consider(a.x-dx,FLOOR-c.radius);consider(a.x+dx,FLOOR-c.radius);
            }
            for(const float x:{LEFT+c.radius,RIGHT-c.radius}) {
                const float dx=x-a.x;
                if(std::abs(dx)<ra)consider(x,a.y-std::sqrt(ra*ra-dx*dx));
            }
            for(unsigned k=j+1;k<i;++k) {
                const auto &b=coins_[k];const float rb=c.radius+b.radius;
                const float dx=b.x-a.x,dy=b.y-a.y,d=std::sqrt(dx*dx+dy*dy);
                if(d<.001f || d>ra+rb || d<std::abs(ra-rb))continue;
                const float along=(ra*ra-rb*rb+d*d)/(2*d);
                const float height=std::sqrt(std::max(0.f,ra*ra-along*along));
                const float x=a.x+along*dx/d,y=a.y+along*dy/d;
                const auto pocket=[&](float px,float py) {
                    if(py<std::min(a.y,b.y) && px>=std::min(a.x,b.x) && px<=std::max(a.x,b.x))consider(px,py);
                };
                pocket(x-height*dy/d,y+height*dx/d);
                pocket(x+height*dy/d,y-height*dx/d);
            }
        }
        // A vertical drop still finds an exact support if no two-contact pocket fits.
        if(best==-1e9f) {
            c.y=FLOOR-c.radius;
            for(unsigned j=0;j<i;++j) {
                const auto &b=coins_[j];const float dx=c.x-b.x,r=c.radius+b.radius;
                if(std::abs(dx)<r)c.y=std::min(c.y,b.y-std::sqrt(r*r-dx*dx));
            }
        }
        c.vx=c.vy=c.angularVelocity=0;c.sleeping=true;c.quietTime=.35f;
    }
    if(animate_last && count)spawn();
}
void CoinPhysicsEngine::supported(std::array<bool,MAX_COINS> &result) const {
    result.fill(false);
    for (unsigned i=0;i<MAX_COINS;++i)
        result[i]=coins_[i].active && coins_[i].y+coins_[i].radius >= FLOOR-SUPPORT_GAP;
    // A support must lead down to the floor. This also wakes an entire column
    // when its base is moved or removed, instead of leaving suspended sleepers.
    for (unsigned pass=0;pass<MAX_COINS;++pass) {
        bool changed=false;
        for (unsigned i=0;i<MAX_COINS;++i) {
            const auto &a=coins_[i];
            if (!a.active || result[i]) continue;
            for (unsigned j=0;j<MAX_COINS;++j) {
                if (!result[j]) continue;
                const auto &b=coins_[j];
                const float dx=b.x-a.x,dy=b.y-a.y,r=a.radius+b.radius+SUPPORT_GAP;
                if (dy > .1f && dx*dx+dy*dy <= r*r) {
                    result[i]=true; changed=true; break;
                }
            }
        }
        if (!changed) break;
    }
}
void CoinPhysicsEngine::substep(float h) {
    std::array<bool,MAX_COINS> supports{};
    supported(supports);
    const float linearDrag=std::exp(-.20f*h),angularDrag=std::exp(-.35f*h);
    const float squashDrag=std::exp(-16.f*h),contactDrag=std::exp(-12.f*h);
    bool moving=false;
    for (unsigned i=0;i<MAX_COINS;++i) {
        auto &c=coins_[i];
        if (!c.active) continue;
        c.age+=h;
        if (c.sleeping && !supports[i]) wake(c);
        if (c.sleeping) continue;
        moving=true;
        c.vy=(c.vy+GRAVITY*h)*linearDrag; c.vx*=linearDrag;
        c.angularVelocity*=angularDrag;
        c.x+=c.vx*h; c.y+=c.vy*h;
        c.rotation=std::fmod(c.rotation+c.angularVelocity*h,6.2831853f);
        c.squash*=squashDrag;
    }
    // A settled pile only needs its support graph checked, not contact solving.
    if (!moving) return;
    for (unsigned pass=0;pass<SOLVER_PASSES;++pass) {
        for (auto &c:coins_) {
            if (!c.active || c.sleeping) continue;
            if (c.x-c.radius < LEFT) {
                c.x=LEFT+c.radius;
                if (c.vx < 0) c.vx=-c.vx*(c.vx < -35 ? c.restitution : 0.f);
            }
            if (c.x+c.radius > RIGHT) {
                c.x=RIGHT-c.radius;
                if (c.vx > 0) c.vx=-c.vx*(c.vx > 35 ? c.restitution : 0.f);
            }
            if (c.y+c.radius >= FLOOR-CONTACT_GAP) {
                c.y=std::min(c.y,FLOOR-c.radius);
                if (c.vy > 0) {
                    const float incoming=c.vy;
                    c.squash=std::max(c.squash,std::min(.12f,incoming/1800.f));
                    c.vy=incoming > 35 ? -incoming*c.restitution : 0;
                    const float friction=std::min(std::abs(c.vx),c.friction*incoming);
                    c.vx-=std::copysign(friction,c.vx);
                }
            }
        }
        for (unsigned i=0;i<MAX_COINS;++i) for (unsigned j=i+1;j<MAX_COINS;++j) {
            auto &a=coins_[i]; auto &b=coins_[j];
            if (!a.active || !b.active || (a.sleeping && b.sleeping)) continue;
            float dx=b.x-a.x,dy=b.y-a.y;
            const float radius=a.radius+b.radius,dist2=dx*dx+dy*dy;
            if (dist2 > (radius+CONTACT_GAP)*(radius+CONTACT_GAP)) continue;
            float distance=std::sqrt(dist2),nx=1,ny=0;
            if (distance > .0001f) { nx=dx/distance; ny=dy/distance; }
            const float approach=(b.vx-a.vx)*nx+(b.vy-a.vy)*ny;
            const float penetration=radius-distance;
            const bool restingA=a.sleeping && supports[i],restingB=b.sleeping && supports[j];
            if (approach < -28.f || penetration > .7f) {
                if (a.sleeping) wake(a);
                if (b.sleeping) wake(b);
            }
            const float massA=a.sleeping ? 0.f : 1.f,massB=b.sleeping ? 0.f : 1.f;
            const float massSum=massA+massB;
            if (massSum == 0) continue;
            if (penetration > 0) {
                const float correction=penetration/massSum;
                a.x-=nx*correction*massA; a.y-=ny*correction*massA;
                b.x+=nx*correction*massB; b.y+=ny*correction*massB;
            }
            if (approach < 0) {
                // A settled, supported pile resists the initial downward hit as
                // a group. Keep positional correction symmetric; side impacts
                // and subsequent movement still use the normal free-coin mass.
                const float impactA=massA*(restingA && std::abs(ny)>.5f ? .2f : 1.f);
                const float impactB=massB*(restingB && std::abs(ny)>.5f ? .2f : 1.f);
                const float impactSum=impactA+impactB;
                const float restitution=approach < -35.f ? std::min(a.restitution,b.restitution) : 0;
                const float impulse=-(1+restitution)*approach/impactSum;
                a.vx-=impulse*nx*impactA; a.vy-=impulse*ny*impactA;
                b.vx+=impulse*nx*impactB; b.vy+=impulse*ny*impactB;
                const float tangent=(b.vx-a.vx)*(-ny)+(b.vy-a.vy)*nx;
                const float limit=std::min(a.friction,b.friction)*impulse;
                const float friction=std::clamp(-tangent/impactSum,-limit,limit);
                a.vx-=friction*(-ny)*impactA; a.vy-=friction*nx*impactA;
                b.vx+=friction*(-ny)*impactB; b.vy+=friction*nx*impactB;
                if (approach < -35.f) {
                    a.squash=std::max(a.squash,std::min(.1f,-approach/2200.f));
                    b.squash=std::max(b.squash,std::min(.1f,-approach/2200.f));
                }
            }
        }
    }
    // The last pair projection can push a neighbor slightly into a boundary.
    // Finish every substep with exact containment so rendered coins never leak.
    for (auto &c:coins_) if (c.active) {
        c.x=std::clamp(c.x,LEFT+c.radius,RIGHT-c.radius);
        c.y=std::min(c.y,FLOOR-c.radius);
    }
    supported(supports);
    for (unsigned i=0;i<MAX_COINS;++i) {
        auto &c=coins_[i];
        if (!c.active || c.sleeping) continue;
        if (supports[i]) {
            // Rotation is cosmetic; contact damping lets the drawn face settle.
            c.angularVelocity*=contactDrag;
            if (c.vx*c.vx+c.vy*c.vy < 9.f) c.quietTime+=h;
            else c.quietTime=0;
            if (c.quietTime >= .35f && std::abs(c.angularVelocity) < .15f) {
                c.vx=c.vy=c.angularVelocity=c.squash=0; c.sleeping=true;
            }
        } else c.quietTime=0;
    }
}
void CoinPhysicsEngine::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0) return;
    dt=std::min(dt,.05f);
    // At most 12 small steps; fixed-capacity storage, no frame heap allocation.
    const int steps=static_cast<int>(std::ceil(dt / (1.f/240.f)));
    const float h=dt/steps;
    for (int step=0;step<steps;++step) substep(h);
}
