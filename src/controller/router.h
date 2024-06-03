#pragma once

#include <string>

#include "fcitx/instance.h"
#include "nlohmann/json.hpp"

#include "current_input_method.h"

inline nlohmann::json handle_controller_request(const std::string& path, fcitx::Instance* instance) {
    auto it = std::find(path.begin(), path.end(), '/');
    std::string method{path.begin(), it};
    if (it != path.end()) it++;
    std::string params{it, path.end()};
    static const std::unordered_map<std::string, std::function<nlohmann::json(const std::string&, fcitx::Instance* instance)>> routes{
        {"current_input_method", current_input_method},
        // {"current_input_method_group", current_input_method_group},
    };
    auto itt = routes.find(method);
    if (itt == routes.end()) return {{ "ERROR", "no such method: " + method }};
    return itt->second(params, instance);
}

