#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FBStructs.h"
#include "FBActors.h"

struct TimedCommand {
    float time = 0.0f;  // seconds since timeline start
    FBCommand command{};
};

using TimedCommandList = std::vector<TimedCommand>;

struct Snapshot {
    Generation generation = 0;
    std::unordered_map<std::string, std::string> eventMap;      // eventTag -> scriptName
    std::unordered_map<std::string, TimedCommandList> scripts;  // scriptName -> timed commands
};

class FBConfig {
public:
    bool LoadInitial();
    bool Reload();

    Generation GetGeneration() const;
    std::shared_ptr<const Snapshot> GetSnapshot() const;
};
