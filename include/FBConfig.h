#pragma once

#include "FBStructs.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

struct TimedCommand
{
    float time = 0.0f;
    FBCommand command{};
};

struct Snapshot
{
    Generation generation = 0;
    std::unordered_map<std::string, std::string> eventMap;
    std::unordered_map<std::string, FBCommandList> scripts;
};

class FBConfig
{
public:
    bool LoadInitial();
    bool Reload();

    Generation GetGeneration() const;
    std::shared_ptr<const Snapshot> GetSnapshot() const;
};