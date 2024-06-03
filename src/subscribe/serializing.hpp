#pragma once

#include <sstream>
#include <string>
#include <unordered_map>

#include "nlohmann/json.hpp"

#include "fcitx-utils/log.h"
#include "fcitx/event.h"

std::unordered_map<std::string, std::string>
extract_params(fcitx::Instance *instance, fcitx::EventType typ,
               fcitx::Event &event) {
    std::unordered_map<std::string, std::string> params;

    // TODO this should be put after InputContextEvent judge
    auto &icEvent = static_cast<fcitx::InputContextEvent &>(event);
    auto *ic = icEvent.inputContext();
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(2);
    for (auto v : ic->uuid()) {
        ss << static_cast<int>(v);
    }
    switch (typ) {
    case fcitx::EventType::InputContextSwitchInputMethod:
        params["input_method"] = instance->inputMethod(ic);
        // fallthrough
    case fcitx::EventType::InputContextFocusIn:
        // fallthrough
    case fcitx::EventType::InputContextFocusOut:
        params["uuid"] = ss.str();
        params["program"] = ic->program();
        params["frontend"] = ic->frontendName();
        break;
    default:
        FCITX_WARN() << "cannot extract params";
    }

    return params;
}

std::string
to_json_str(const std::string &ev,
            const std::unordered_map<std::string, std::string> &params) {
    nlohmann::json j{
        {"event", ev},
        {"params", nlohmann::json::object()},
    };
    for (const auto &[k, v] : params) {
        j["params"][k] = v;
    }
    return j.dump();
}
