#include "button_logic.h"
ButtonAction ButtonLogic::update(bool pressed, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; edge_at_ = now; }
    if (raw_ != stable_ && uint32_t(now - edge_at_) >= 30) {
        stable_ = raw_;
        if (stable_) { press_at_ = now; reset_sent_ = false; }
        else {
            if (reset_sent_) return ButtonAction::None;
            const uint32_t held = now - press_at_;
            if (held >= 10000) return ButtonAction::Reset;
            if (held >= 5000) return ButtonAction::Setup;
            return ButtonAction::Page;
        }
    }
    if (stable_ && !reset_sent_ && uint32_t(now - press_at_) >= 10000) {
        reset_sent_ = true;
        return ButtonAction::Reset;
    }
    return ButtonAction::None;
}
