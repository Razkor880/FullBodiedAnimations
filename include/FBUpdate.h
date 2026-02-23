#pragma once

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


private:
    FBConfig& _config;
    FBEvents& _events;
    float _timeSeconds{0.0f};
};



