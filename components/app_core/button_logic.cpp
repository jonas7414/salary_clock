#include "button_logic.h"
ButtonAction ButtonLogic::update(bool pressed, uint32_t now) {
    // Defer a single click so a double click does not also change pages.
    bool page_due=false;
    if (click_pending_ && !raw_ && uint32_t(now-click_at_)>=400) {
        click_pending_=false;
        page_due=true;
    }
    if (pressed!=raw_) { raw_=pressed; edge_at_=now; }
    if (raw_!=stable_ && uint32_t(now-edge_at_)>=30) {
        stable_=raw_;
        if (stable_) {
            second_click_=click_pending_ && uint32_t(edge_at_-click_at_)<400;
            click_pending_=false;
            press_at_=now;
            sleep_sent_=false;
        } else {
            if (sleep_sent_) return ButtonAction::None;
            const uint32_t held=now-press_at_;
            if (held>=5000) return ButtonAction::Sleep;
            // A cancelled long hold must never enter setup or change pages.
            if (held>=500) { second_click_=false; return ButtonAction::None; }
            if (second_click_) { second_click_=false; return ButtonAction::Setup; }
            click_pending_=true;
            click_at_=now;
        }
    }
    if (stable_ && !sleep_sent_ && uint32_t(now-press_at_)>=5000) {
        sleep_sent_=true;
        click_pending_=second_click_=false;
        return ButtonAction::Sleep;
    }
    return page_due ? ButtonAction::Page : ButtonAction::None;
}
