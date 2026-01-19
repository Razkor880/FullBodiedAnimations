#include "FBConfig.h"

#include <memory>

namespace
{
    std::shared_ptr<const Snapshot> g_snapshot;
}

bool FBConfig::LoadInitial()
{ 
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->generation = 1;

    snapshot->eventMap.clear();
    snapshot->scripts.clear();

    snapshot->eventMap["FB_TestEvent"] = "TestScript";

    FBCommand cmd{};
    cmd.type = FBCommandType::Transform;
    cmd.role = ActorRole::Self;
    cmd.generation = snapshot->generation;
    cmd.opcode = "Scale";
    cmd.target = "NPC Head [Head]";
    cmd.args = "0.8";
    
    snapshot->scripts["TestScript"].push_back(cmd);


    g_snapshot = snapshot;
    return true;
}

bool FBConfig::Reload() {
    if (!g_snapshot) {
        return false;
    }

    auto snapshot = std::make_shared<Snapshot>(*g_snapshot);
    snapshot->generation = g_snapshot->generation + 1;

    // re-parse config will go here

    g_snapshot = snapshot;
    return true;
}

Generation FBConfig::GetGeneration() const
{
    return g_snapshot ? g_snapshot->generation : 0;
}

std::shared_ptr<const Snapshot> FBConfig::GetSnapshot() const
{
    return g_snapshot;
}

