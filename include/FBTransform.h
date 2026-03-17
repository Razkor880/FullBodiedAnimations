#pragma once
#include <array>
#include<string_view>

namespace RE {
    class Actor;

}

class FBTransform {
public:
    static void ApplyScale(RE::Actor* actor, std::string_view nodeName, float scale);
    static void ApplyScale_MainThread(RE::Actor* actor, std::string_view nodeName, float scale);

    static bool TryGetScale(RE::Actor* actor, std::string_view nodeName, float& outScale);
    static void ApplyTranslate(RE::Actor* actor, std::string_view nodeName, float x, float y, float z);
    static void ApplyTranslate_MainThread(RE::Actor* actor, std::string_view nodeName, float x, float y, float z);
    static bool TryGetTranslate(RE::Actor* actor, std::string_view nodeName, std::array<float, 3>& outTranslate);
};



 

