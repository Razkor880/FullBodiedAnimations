#pragma once

#include <cstdint>
#include <string>
#include <vector>



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

using FBCommandList = std::vector<FBCommand>;