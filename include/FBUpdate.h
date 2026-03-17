#pragma once
#include <unordered_map>
#include <array>
#include <atomic>
#include <memory>
#include "FBStructs.h"

class FBConfig;
class FBEvents;

class FBUpdate {
public:
    FBUpdate(FBConfig& config, FBEvents& events);
    void Tick(float dtSeconds);
    void ApplyPostAnimSustainForActor(RE::Actor* actor, std::uint8_t phase);


    std::vector<ActiveTimeline> _activeTimelines;
    Generation _lastSeenGeneration = 0;
    std::uint64_t _postAnimEpoch = 0;

    // Tracks last epoch+phase applied for an actor:
    // key = formID, value = (epoch<<1) | phaseBit (0 or 1)
    std::unordered_map<std::uint32_t, std::uint64_t> _postAnimLastEpochPhaseByActor;

    // Called from NiAVObject::UpdateWorldData hook.
    // If this object is in our move registry, apply offset additively to local.translate.
    void ApplyWorldDataSustainForObject(RE::NiAVObject* obj);

    // Called from post-anim sustain phase 0 to (re)register any moved nodes for this actor.
    void RefreshMoveRegistryForActor(RE::Actor* actor, std::uint8_t phase);

    struct ActiveTween {
        FBEvent event{};
        ActorRole role{ActorRole::Self};

        std::string channelKey;
        std::string target;

        float startTimeSeconds{0.0f};
        float durationSeconds{0.0f};
        float startValue{0.0f};
        float endValue{0.0f};

        Easing easing{Easing::Linear};
        Generation generation{0};
        FBCommandType type{FBCommandType::Transform};
        bool startCaptured = false;
    };

    std::unordered_map<std::string, ActiveTween> _activeTweens;
    std::unordered_map<std::string, float> _lastMorphValue;

private:
    FBConfig& _config;
    FBEvents& _events;
    float _timeSeconds{0.0f};

    // Registry: exact NiAVObject* -> sustained offset.
    // This is what the UpdateWorldData hook consults.
    std::unordered_map<RE::NiAVObject*, std::array<float, 3>> _moveRegistry;

    // Optional: throttled proof-of-life counter
    std::uint32_t _moveRegistryStompCounter = 0;
};



