#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FBStructs.h"
#include "FBActors.h"

struct Snapshot {
    Generation generation = 0;
    bool ResetOnPairEnd = false;
    float ResetDelay = 0.0f;
    float DefaultTweenScale = 0.0f;
    float DefaultTweenMorph = 0.0f;
    std::unordered_map<std::string, std::string> eventMap;
    std::unordered_map<std::string, TimedCommandList> scripts;
    bool SwapCasterTarget = false;

};



class FBConfig {
public:
    bool LoadInitial();
    bool Reload();

    Generation GetGeneration() const {
        auto snap = GetSnapshot();
        return snap ? snap->generation : 0;
    }
    std::shared_ptr<const Snapshot> GetSnapshot() const;
};
