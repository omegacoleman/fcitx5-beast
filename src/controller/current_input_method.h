#pragma once

#include <string>

#include "nlohmann/json.hpp"

inline nlohmann::json current_input_method(const std::string&, fcitx::Instance* instance) {
    return {{ "input_method", instance->currentInputMethod() }};
}

