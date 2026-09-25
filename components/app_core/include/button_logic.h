#pragma once
#include <cstdint>
enum class ButtonAction { None, Page, Setup, Sleep };
class ButtonLogic {
public:
    ButtonAction update(bool pressed, uint32_t now_ms);
    bool pressed() const { return stable_; }
    uint32_t held_ms(uint32_t now_ms) const { return stable_ ? now_ms - press_at_ : 0; }
private:
    bool raw_{}, stable_{}, sleep_sent_{}, click_pending_{}, second_click_{};
    uint32_t edge_at_{}, press_at_{}, click_at_{};
};
