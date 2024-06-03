#pragma once

#include <string>
#include <unordered_map>

#include "fcitx/event.h"

inline const std::unordered_map<std::string, fcitx::EventType> &ev_map() {
    static const std::unordered_map<std::string, fcitx::EventType> ev_map{
        {"input_context_focus_in", fcitx::EventType::InputContextFocusIn},
        {"input_context_focus_out", fcitx::EventType::InputContextFocusOut},
        {"input_context_switch_input_method",
         fcitx::EventType::InputContextSwitchInputMethod},
    };

    return ev_map;
}
