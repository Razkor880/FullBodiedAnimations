#pragma once

#include <string_view>

namespace RE {
    class Actor;
}

namespace FBTransform {
    bool ApplyScale(RE::Actor* actor, std::string_view nodeName, float scale);
    bool TryGetScale(RE::Actor* actor, std::string_view nodeName, float& outScale);
}
