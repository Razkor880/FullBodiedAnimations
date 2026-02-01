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

    std::vector<ActiveTimeline> _activeTimelines;
    

private:
    FBConfig& _config;
    FBEvents& _events;
    float _timeSeconds{0.0f};
};



