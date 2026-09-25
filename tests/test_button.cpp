#include "button_logic.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } ++checks; } while(0)
static unsigned checks;
static void press(ButtonLogic &b,uint32_t t) {
    CHECK(b.update(true,t)==ButtonAction::None);
    CHECK(b.update(true,t+30)==ButtonAction::None);
}
static ButtonAction release(ButtonLogic &b,uint32_t t) {
    CHECK(b.update(false,t)==ButtonAction::None);
    return b.update(false,t+30);
}
int main() {
    { ButtonLogic b; press(b,0);
      CHECK(release(b,100)==ButtonAction::None);
      CHECK(b.update(false,529)==ButtonAction::None);
      CHECK(b.update(false,530)==ButtonAction::Page);
      CHECK(b.update(false,1000)==ButtonAction::None); }
    { ButtonLogic b; press(b,0); release(b,100); press(b,300);
      CHECK(release(b,400)==ButtonAction::Setup);
      CHECK(b.update(false,1000)==ButtonAction::None); }
    { ButtonLogic b; press(b,0);
      CHECK(b.update(true,5029)==ButtonAction::None);
      CHECK(b.update(true,5030)==ButtonAction::Sleep);
      for (uint32_t t=5040;t<30000;t+=10) CHECK(b.update(true,t)==ButtonAction::None);
      CHECK(release(b,30000)==ButtonAction::None);
      CHECK(b.update(false,31000)==ButtonAction::None); }
    { ButtonLogic b; press(b,0); release(b,100); press(b,300);
      CHECK(b.update(true,5330)==ButtonAction::Sleep);
      CHECK(release(b,5400)==ButtonAction::None);
      CHECK(b.update(false,6000)==ButtonAction::None); }
    for (uint32_t duration : {500U,1000U,4990U}) {
      ButtonLogic b; press(b,0);
      CHECK(release(b,duration)==ButtonAction::None);
      CHECK(b.update(false,duration+500)==ButtonAction::None);
    }
    { ButtonLogic b; const uint32_t t=UINT32_MAX-1000;
      press(b,t); CHECK(b.update(true,t+5030)==ButtonAction::Sleep);
      CHECK(release(b,t+6000)==ButtonAction::None); }
    { ButtonLogic b; const uint32_t t=UINT32_MAX-200;
      press(b,t); release(b,t+100); press(b,t+300);
      CHECK(release(b,t+400)==ButtonAction::Setup); }
    { ButtonLogic b;
      CHECK(b.update(true,0)==ButtonAction::None);
      CHECK(b.update(false,10)==ButtonAction::None);
      CHECK(b.update(true,20)==ButtonAction::None);
      CHECK(b.update(true,49)==ButtonAction::None && !b.pressed());
      CHECK(b.update(true,50)==ButtonAction::None && b.pressed());
      CHECK(release(b,100)==ButtonAction::None);
      CHECK(b.update(false,530)==ButtonAction::Page); }
    std::printf("PASS: %u single-button checks\n",checks);
}
