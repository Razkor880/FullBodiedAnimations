#include "FBUpdate.h"
#include "FBTransform.h"
#include "FBConfig.h"
#include "FBEvents.h"

#include <RE/Skyrim.h>
#include "SKSE/SKSE.h"
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <string_view>


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

static RE::Actor* ResolveActorForEvent(const FBEvent& e, ActorRole role) {
    (void)role;
    ActorKey key = e.actor;
    if (!key.IsValid()) {
        return nullptr;
    }

    auto* form = RE::TESForm::LookupByID(key.formID);
    if (!form) {
        return nullptr;
    }

    return form->As<RE::Actor>();
}



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
    return FBTransform::ApplyScale(actor, cmd.target, scale);
}

void FBUpdate::Tick(float dtSeconds) {
    const auto snap = _config.GetSnapshot();
    if (!snap) {
        spdlog::warn("[FB] Tick(dt={}): no config snapshot", dtSeconds);
        return;
    }

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

        const auto& commands = scriptIt->second;

        spdlog::info("[FB] Tick: event '{}' actor=0x{:08X} -> script '{}' ({} commands)", e.tag, e.actor.formID,
                     scriptName, commands.size());

        if (!commands.empty()) 
        {
            const auto& c0 = commands.front();
            spdlog::info("[FB] Tick: first cmd opcode='{}' target='{}' args='{}' gen={}", c0.opcode, c0.target, c0.args,
                         c0.generation);
            RE::Actor* actor = ResolveActorForEvent(e, c0.role);

            


        }


        

        for (const auto& cmd : commands) 
        {
            if (!cmd.IsValid()) 
            {
                spdlog::warn("[FB] Tick: invalid cmd opcode= '{}' gen={}", cmd.opcode, cmd.generation);
                continue;
            }
            
            switch (cmd.type) 
            { 
            case FBCommandType::Transform:
                    if (cmd.opcode == "Scale") 
                    {
                        ExecuteTransformCommand(cmd, e);
                    } else 
                    {
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

// ---------------- Pump ----------------

FBUpdatePump::FBUpdatePump(FBUpdate& update) : _update(update) {}

FBUpdatePump::~FBUpdatePump() { Stop(); }

void FBUpdatePump::SetTickHz(double hz) {
    if (hz <= 0.0) {
        hz = 60.0;
    }
    _tickHz.store(hz);
}

void FBUpdatePump::Start() {
    if (_running.exchange(true)) {
        return;  // already running
    }

    _thread = std::thread([this]() { ThreadMain(); });
    spdlog::info("[FB] UpdatePump started");
}

void FBUpdatePump::Stop() {
    if (!_running.exchange(false)) {
        return;  // already stopped
    }

    if (_thread.joinable()) {
        _thread.join();
    }

    spdlog::info("[FB] UpdatePump stopped");
}

void FBUpdatePump::ThreadMain() {
    using clock = std::chrono::steady_clock;

    auto prev = clock::now();

    while (_running.load()) {
        const auto hz = _tickHz.load();
        const auto period = std::chrono::duration<double>(1.0 / hz);

        std::this_thread::sleep_for(period);

        const auto now = clock::now();
        const std::chrono::duration<double> dt = now - prev;
        prev = now;

        const float dtSeconds = static_cast<float>(dt.count());

        if (auto task = SKSE::GetTaskInterface(); task) {
            task->AddTask([this, dtSeconds]() { _update.Tick(dtSeconds); });
        }
    }
}
