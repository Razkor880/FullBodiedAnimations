#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

using Generation = std::uint64_t;


enum class Easing : std::uint8_t 
{
    Linear
};

enum class ActorRole : std::uint8_t 
{
    Self, 
    Caster, 
    Target 
};

enum class FBCommandType : std::uint8_t 
{
    Transform,
    Morph,
    Fx,
    State
};

struct TweenSpec 
{
    float duration = 0.0f; //seconds
    float delay = 0.0f; //seconds
    Easing easing = Easing::Linear;
};

struct ActorKey 
{
    std::uint32_t formID = 0;

    [[nodiscard]] bool IsValid() const noexcept { return formID != 0; }

    friend bool operator==(const ActorKey& a, const ActorKey& b) noexcept {
        return a.formID == b.formID;
    }
};

struct FBEvent 
{
    std::string tag{};
    ActorKey actor{};
    std::uint8_t retries = 0;

    [[nodiscard]] bool IsValid() const noexcept 
    { return !tag.empty() && actor.IsValid();
    }
};

struct FBCommand 
{
    FBCommandType type = FBCommandType::Transform;

    ActorRole role = ActorRole::Self;
    Generation generation = 0;

    TweenSpec tween{};

    std::string opcode;
    std::string target;
    std::string args;

    [[nodiscard]] bool IsValid() const noexcept 
    {
        return generation != 0 && !opcode.empty();
    }
};



struct ActiveTimeline
{
    FBEvent event;
    std::string scriptKey;
    float elapsed = 0.0f;
    std::size_t nextIndex = 0;
    std::uint64_t generation = 0;
    std::unordered_map<std::string, float> originalScale;
    std::unordered_map<std::string, std::array<float, 3>> originalTranslate;
    bool commandsComplete = false;
    float startTimeSeconds = 0.0f;
    bool resetScheduled = false;
    double resetAtSeconds = 0.0;
    bool touchedMorphCaster = false;
    bool touchedMorphTarget = false;
    std::unordered_set<std::string> touchedMorphsCaster;
    std::unordered_set<std::string> touchedMorphsTarget;
    std::unordered_map<std::string, float> sustainMorphsCaster;
    std::unordered_map<std::string, float> sustainMorphsTarget;

    // Sustained Move is now an OFFSET (added on top of animated base pose).
    std::unordered_map<std::string, std::array<float, 3>> sustainTranslateCaster;
    std::unordered_map<std::string, std::array<float, 3>> sustainTranslateTarget;

    // Per-frame base pose cache captured in phase 0 (so later phases don't double-add).
    std::unordered_map<std::string, std::array<float, 3>> sustainTranslateBaseCaster;
    std::unordered_map<std::string, std::array<float, 3>> sustainTranslateBaseTarget;
    // optional throttle so we don't spam Papyrus every frame
    float nextSustainAtSeconds = 0.0f;

    // Additive sustain support: last value we applied (to detect double-apply and derive base).
    std::unordered_map<std::string, std::array<float, 3>> sustainTranslateLastAppliedCaster;
    std::unordered_map<std::string, std::array<float, 3>> sustainTranslateLastAppliedTarget;


};

using FBCommandList = std::vector<FBCommand>;

struct TimedCommand {
    float time = 0.0f;  // seconds since timeline start
    FBCommand command{};
};

using TimedCommandList = std::vector<TimedCommand>;
