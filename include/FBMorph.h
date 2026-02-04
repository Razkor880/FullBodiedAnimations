#pragma once
#include <string_view>

namespace RE {
    class Actor;
 }

namespace FB::Morph {

    //Papyrus bridge class and functions
    inline constexpr const char* kBridgeClass = "FBMorphBridge";
    inline constexpr const char* kFnSetMorph = "FBSetMorph";
    inline constexpr const char* kFnClear = "FBClearMorph";


    //Main-thread calls into Papyrus VM
    void Set_MainThread(RE::Actor* actor, std::string_view morphName, float value);
    void Set(RE::Actor* actor, std::string_view morphName, float value);

    // Clear a specific morph (by name/key)
    void Clear_MainThread(RE::Actor* actor, std::string_view morphName);
    void Clear(RE::Actor* actor, std::string_view morphName);

 }