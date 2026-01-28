#include "FBUpdate.h"
#include "FBPlugin.h"

#include <RE/Skyrim.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string_view>
#include <chrono>
#include <thread>

#include "FBConfig.h"
#include "FBEvents.h"
#include "FBTransform.h"


FBUpdatePump::FBUpdatePump(FBUpdate& update) : _update(update) {}

void FBUpdatePump::Start() {
    if (_running.exchange(true)) {
        return;  // already running
    }

    // Minimal: run on a detached thread; you can upgrade to jthread later.
    std::thread([this]() {
        using clock = std::chrono::steady_clock;

        auto last = clock::now();
        while (_running.load()) {
            const auto now = clock::now();
            const std::chrono::duration<float> dt = now - last;
            last = now;

            if (auto* task = SKSE::GetTaskInterface(); task) {
                const float dtSeconds = dt.count();
                task->AddTask([this, dtSeconds]() { _update.Tick(dtSeconds); });
            }




            // Minimal throttle ~60Hz.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }).detach();
}

void FBUpdatePump::Stop() { _running.store(false); }



FBUpdate::FBUpdate(FBConfig& config, FBEvents& events) : _config(config), _events(events) {}

static bool TryParseFloat(std::string_view s, float& out) {
    if (auto pos = s.find('='); pos != std::string_view::npos) {
        s = s.substr(pos + 1);
    }

    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);

    std::string tmp(s);
    char* end = nullptr;
    out = std::strtof(tmp.c_str(), &end);
    return end != tmp.c_str();
}

static RE::Actor* ResolveActorForEvent(const FBEvent& e, ActorRole /*role*/) {
    if (!e.actor.IsValid()) {
        return nullptr;
    }

    auto* form = RE::TESForm::LookupByID(e.actor.formID);
    if (!form) {
        return nullptr;
    }

    return form->As<RE::Actor>();
}

struct PendingTransform {
    FBCommand cmd;
    FBEvent ctx;
    float nextAttempt = 0.0f;
    std::uint8_t tries = 0;
};

static std::vector<PendingTransform> g_pendingTransforms;
static float g_timeSeconds = 0.0f;  // advance with dt in your update pump

static bool ExecuteTransformCommand(const FBCommand& cmd, const FBEvent& ctxEvent) {
    float scale = 1.0f;
    if (!TryParseFloat(cmd.args, scale)) {
        spdlog::warn("[FB] Transform: failed to parse scale from args='{}'", cmd.args);
        return false;
    }

    RE::Actor* actor = ResolveActorForEvent(ctxEvent, cmd.role);
    if (!actor) {
        spdlog::info("[FB] Transform: could not resolve actor for role={} formID=0x{:08X}",
                     static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
        return false;
    }

    spdlog::info("[FB] Exec: Scale actor=0x{:08X} node='{}' scale={}", actor->formID, cmd.target, scale);

    if (FBTransform::ApplyScale(actor, cmd.target, scale)) {
        return true;

    }

    // Not ready (likely no 3D yet). Retry later.
    PendingTransform pending;
    pending.cmd = cmd;
    pending.ctx = ctxEvent;
    pending.nextAttempt = g_timeSeconds + 0.1f;  // 100ms spacing
    pending.tries = 1;
    g_pendingTransforms.push_back(std::move(pending));

    spdlog::info("[FB] Transform: queued retry actor=0x{:08X} node='{}' tries={}", actor->formID, cmd.target, 1);
    return false;

    for (std::size_t i = 0; i < g_pendingTransforms.size();) {
        auto& p = g_pendingTransforms[i];
        if (g_timeSeconds < p.nextAttempt) {
            ++i;
            continue;
        }

        const bool ok = FBTransform::ApplyScale(actor, cmd.target, scale);
        if (!ok) {
            spdlog::info("[FB] Exec: Scale failed (will not apply) actor=0x{:08X} node='{}'", actor->formID,
                         cmd.target);
        }
        return ok;


        // If ExecuteTransformCommand re-queued again, we should remove this one to avoid duplicates.
        // Easiest: handle retries here directly instead of calling ExecuteTransformCommand.
        // So: replace the above "ok = ExecuteTransformCommand" with inline attempt (I can provide if you want).
        ++i;
    }
}


void FBUpdate::Tick(float dtSeconds) {
    const auto snap = _config.GetSnapshot();
    if (!snap) {
        spdlog::warn("[FB] Tick(dt={}): no config snapshot", dtSeconds);
        return;
    }
    g_timeSeconds += dtSeconds;

    auto events = _events.Drain();

    if (!events.empty()) {
        spdlog::info("[FB] Tick(dt={}): gen={} drainedEvents={}", dtSeconds, snap->generation, events.size());
    }

    for (const auto& e : events) {
        if (!e.IsValid()) {
            spdlog::warn("[FB] Tick: invalid event (tag='{}' actor=0x{:08X})", e.tag, e.actor.formID);
            continue;
        }

        const auto mapIt = snap->eventMap.find(e.tag);
        if (mapIt == snap->eventMap.end()) {
            spdlog::info("[FB] Tick: event '{}' actor=0x{:08X} -> no mapping", e.tag, e.actor.formID);
            continue;
        }

        const auto& scriptName = mapIt->second;


        

        const auto scriptIt = snap->scripts.find(scriptName);
        if (scriptIt == snap->scripts.end()) {
            spdlog::warn("[FB] Tick: event '{}' mapped to script '{}' but script not found", e.tag, scriptName);
            continue;
        }

        // Phase 1.5: scripts are timed commands
        const auto& timed = scriptIt->second;

        spdlog::info("[FB] Tick: event '{}' actor=0x{:08X} -> script '{}' ({} timed commands)", e.tag, e.actor.formID,
                     scriptName, timed.size());

        if (!timed.empty()) {
            const auto& tc0 = timed.front();
            const auto& c0 = tc0.command;
            spdlog::info("[FB] Tick: first cmd t={} opcode='{}' target='{}' args='{}' gen={}", tc0.time, c0.opcode,
                         c0.target, c0.args, c0.generation);
        }

        // Minimal behavior (no scheduling yet): execute immediately.
        // Next Phase 1.5 patch will add timeline instances and elapsed-time firing.
        for (const auto& tc : timed) {
            const auto& cmd = tc.command;

            if (!cmd.IsValid()) {
                spdlog::warn("[FB] Tick: invalid cmd opcode='{}' gen={}", cmd.opcode, cmd.generation);
                continue;
            }

            switch (cmd.type) {
                case FBCommandType::Transform:
                    if (cmd.opcode == "Scale") {
                        ExecuteTransformCommand(cmd, e);
                    } else {
                        spdlog::info("[FB] Exec: unsupported Transform opcode='{}'", cmd.opcode);
                    }
                    break;

                case FBCommandType::Morph:
                case FBCommandType::Fx:
                case FBCommandType::State:
                default:
                    spdlog::info("[FB] Exec: cmd type {} not implemented (opcode='{}')",
                                 static_cast<std::uint32_t>(cmd.type), cmd.opcode);
                    break;
            }
        }
    }
}
