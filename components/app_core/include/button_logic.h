#pragma once
#include <cstdint>
enum class ButtonAction { None, Page, Setup, Reset };
class ButtonLogic {
public:
    ButtonAction update(bool pressed, uint32_t now_ms);
    bool pressed() const { return stable_; }
    uint32_t held_ms(uint32_t now_ms) const { return stable_ ? now_ms - press_at_ : 0; }
private:
    bool raw_{}, stable_{}, reset_sent_{};
    uint32_t edge_at_{}, press_at_{};
};
