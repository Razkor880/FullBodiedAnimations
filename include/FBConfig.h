#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FBStructs.h"
#include "FBActors.h"

//struct TimedCommand {
//    float time = 0.0f;  // seconds since timeline start
//    FBCommand command{};
//};


struct Snapshot {
    Generation generation = 0;
    bool ResetOnPairEnd = false;

    std::unordered_map<std::string, std::string> eventMap;
    std::unordered_map<std::string, TimedCommandList> scripts;
};


class FBConfig {
public:
    bool LoadInitial();
    bool Reload();

    Generation GetGeneration() const;
    std::shared_ptr<const Snapshot> GetSnapshot() const;
};
