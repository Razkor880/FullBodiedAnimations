#include "FBConfig.h"

#include <memory>

namespace {
    std::shared_ptr<const Snapshot> g_snapshot;
}

bool FBConfig::LoadInitial() {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->generation = 1;

    snapshot->eventMap.clear();
    snapshot->scripts.clear();

    // Minimal hardcoded test wiring (Phase 1.5 data shape)
    snapshot->eventMap["FB_TestEvent"] = "TestScript";

    FBCommand cmd{};
    cmd.type = FBCommandType::Transform;
    cmd.role = ActorRole::Self;
    cmd.generation = snapshot->generation;
    cmd.opcode = "Scale";
    cmd.target = "NPC Head [Head]";
    cmd.args = "0.8";

    TimedCommand tc{};
    tc.time = 0.0f;
    tc.command = std::move(cmd);

    snapshot->scripts["TestScript"].push_back(std::move(tc));

    g_snapshot = std::move(snapshot);
    return true;
}

bool FBConfig::Reload() {
    if (!g_snapshot) {
        return false;
    }

    // Minimal: generation bump, future parsing will rebuild maps/scripts.
    auto snapshot = std::make_shared<Snapshot>(*g_snapshot);
    snapshot->generation = g_snapshot->generation + 1;

    // TODO: reload/parse INI and rebuild snapshot->eventMap and snapshot->scripts

    g_snapshot = std::move(snapshot);
    return true;
}

Generation FBConfig::GetGeneration() const { return g_snapshot ? g_snapshot->generation : 0; }

std::shared_ptr<const Snapshot> FBConfig::GetSnapshot() const { return g_snapshot; }
