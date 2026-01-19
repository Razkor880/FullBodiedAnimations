#pragma once

#include <string_view>

namespace RE {
    class Actor;
}

class FBTransform
{
public:
    static bool ApplyScale(RE::Actor* actor, std::string_view nodeName, float scale);
};