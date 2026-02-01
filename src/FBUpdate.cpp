#include "FBUpdate.h"
#include "FBStructs.h"
#include "FBActors.h"
#include "FBTransform.h"
#include "FBConfig.h"

#include <RE/Skyrim.h>
#include <spdlog/spdlog.h>


#include "FBEvents.h"

#include "FBExec.h" 
#include <unordered_map>
#include <string_view>
#include <string>


FBUpdate::FBUpdate(FBConfig& config, FBEvents& events) : _config(config), _events(events) {}


static std::string MakeRoleNodeKey(ActorRole role, std::string_view nodeName) {
    std::string key;
    key.reserve(2 + nodeName.size());
    key.push_back(role == ActorRole::Target ? 'T' : 'C');
    key.push_back('|');
    key.append(nodeName.data(), nodeName.size());
    return key;
}

static auto FindActiveTimelineIter(std::vector<ActiveTimeline>& timelines, const FBEvent& e,
                                   const std::string& scriptKey) {
    return std::find_if(timelines.begin(), timelines.end(), [&](const ActiveTimeline& tl) {
        return tl.event.actor.formID == e.actor.formID && tl.scriptKey == scriptKey;
    });
}


static void CaptureOriginalScaleIfNeeded(ActiveTimeline& tl, const FBCommand& cmd) {
    // Only capture for scale transforms
    if (cmd.type != FBCommandType::Transform || cmd.opcode != "Scale") {
        return;
    }

    // Build key (role + node)
    auto key = MakeRoleNodeKey(cmd.role, cmd.target);
    if (tl.originalScale.find(key) != tl.originalScale.end()) {
        return;  // already captured
    }

    // Resolve actor for the role
    RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, cmd.role);
    if (!actor) {
        return;
    }

    // Read current scale from node
    float current = 1.0f;
    if (!FBTransform::TryGetScale(actor, cmd.target, current)) {
        return;
    }

    tl.originalScale.emplace(std::move(key), current);

    spdlog::info("[FB] Reset: captured actor=0x{:08X} role={} node='{}' scale={}", actor->formID,
                 (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, current);
}

static void ApplyReset(const ActiveTimeline& tl) {
    if (tl.originalScale.empty()) {
        return;
    }

    for (const auto& [key, original] : tl.originalScale) {
        if (key.size() < 3) {
            continue;
        }

        const char roleChar = key[0];
        const std::string_view nodeName(key.c_str() + 2);

        const ActorRole role = (roleChar == 'T') ? ActorRole::Target : ActorRole::Caster;
        RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, role);
        if (!actor) {
            continue;
        }

        float dummy = 0.0f;
        if (FBTransform::TryGetScale(actor, nodeName, dummy)) {
            FBTransform::ApplyScale(actor, nodeName, original);
            spdlog::info("[FB] Reset: applied actor=0x{:08X} role={} node='{}' scale={}", actor->formID,
                         (roleChar == 'T' ? "T" : "C"), nodeName, original);
        }

    }
}


void FBUpdate::Tick(float dtSeconds) {
    const auto snap = _config.GetSnapshot();

    if (_lastSeenGeneration != snap->generation) {
        spdlog::info("[FB] Generation change {} -> {}; dropping {} timelines", _lastSeenGeneration, snap->generation,
                     _activeTimelines.size());

        if (snap->ResetOnPairEnd) {
            for (auto& tl : _activeTimelines) {
                ApplyReset(tl);
            }
        }

        _activeTimelines.clear();
        _lastSeenGeneration = snap->generation;
    }


    if (!snap) {
        spdlog::warn("[FB] Tick(dt={}): no config snapshot", dtSeconds);
        return;
    }

    _timeSeconds += dtSeconds;

    // 1) Drain events
    auto events = _events.Drain();
    if (!events.empty()) {
        spdlog::info("[FB] Tick(dt={}): gen={} drainedEvents={}", dtSeconds, snap->generation, events.size());
    }

    // 2) Create/Reset timelines from events
    for (const auto& e : events) {
        if (!e.IsValid()) {
            spdlog::warn("[FB] Tick: invalid event (tag='{}' actor=0x{:08X})", e.tag, e.actor.formID);
            continue;
        }

        const auto& eventTag = e.tag;

        const auto mapIt = snap->eventMap.find(eventTag);
        if (mapIt == snap->eventMap.end()) {
            spdlog::info("[FB] Tick: event '{}' actor=0x{:08X} -> no mapping", eventTag, e.actor.formID);
            continue;
        }

        const auto& scriptKey = mapIt->second;

        // PairEnd is a clip-end marker: close an existing timeline, do NOT start/reset.
        if (e.tag == "PairEnd") {
            auto it = FindActiveTimelineIter(_activeTimelines, e, scriptKey);
            if (it != _activeTimelines.end()) {
                if (snap->ResetOnPairEnd) {  // NOTE: correct casing
                    ApplyReset(*it);
                }
                spdlog::info("[FB] Timeline: CLOSE (PairEnd) actor=0x{:08X} scriptKey='{}'", e.actor.formID, scriptKey);
                _activeTimelines.erase(it);
            } else {
                spdlog::info("[FB] PairEnd: no active timeline actor=0x{:08X} scriptKey='{}'", e.actor.formID,
                             scriptKey);
            }
            continue;
        }




        const auto scriptIt = snap->scripts.find(scriptKey);
        if (scriptIt == snap->scripts.end()) {
            spdlog::warn("[FB] Tick: event '{}' mapped to script '{}' but script not found", eventTag, scriptKey);
            continue;
        }

        // Policy: 1 active timeline per actor (by formID)
        auto findIt = std::find_if(_activeTimelines.begin(), _activeTimelines.end(), [&](const ActiveTimeline& tl) {
            return tl.event.IsValid() && tl.event.actor.formID == e.actor.formID;
        });

        if (findIt == _activeTimelines.end()) {
            ActiveTimeline tl{};
            tl.startTimeSeconds = _timeSeconds;
            tl.event = e;
            tl.scriptKey = scriptKey;
            tl.elapsed = 0.0f;
            tl.nextIndex = 0;
            tl.generation = snap->generation;
            tl.commandsComplete = false;
            _activeTimelines.emplace_back(std::move(tl));


            spdlog::info("[FB] Timeline: START actor=0x{:08X} eventTag='{}' scriptKey='{}' gen={} ({} cmds)",
                         e.actor.formID, eventTag, scriptKey, snap->generation, scriptIt->second.size());
        } else {
            findIt->event = e;
            findIt->scriptKey = scriptKey;
            findIt->startTimeSeconds = _timeSeconds;
            findIt->elapsed = 0.0f;
            findIt->nextIndex = 0;
            findIt->generation = snap->generation;
            findIt->commandsComplete = false;

            spdlog::info("[FB] Timeline: RESET actor=0x{:08X} eventTag='{}' scriptKey='{}' gen={} ({} cmds)",
                         e.actor.formID, eventTag, scriptKey, snap->generation, scriptIt->second.size());
        }
        


    }
    std::size_t guard = 0;

    // 3) Tick active timelines and fire due commands
    for (std::size_t i = 0; i < _activeTimelines.size(); /* manual */) {
        if (++guard > 100000) {
            spdlog::critical("[FB] Tick: guard tripped (timeline loop spin) timelines={}", _activeTimelines.size());
            break;
        }

        auto& tl = _activeTimelines[i];

        // Always recompute elapsed from subtraction (fine)
        tl.elapsed = _timeSeconds - tl.startTimeSeconds;

        // Find script commands for this timeline
        const auto itScript = snap->scripts.find(tl.scriptKey);
        if (itScript == snap->scripts.end()) {
            // Drop timeline if script missing
            if (snap->ResetOnPairEnd) {
                ApplyReset(tl);
            }
            spdlog::info("[FB] Timeline: DROP missing scriptKey='{}'", tl.scriptKey);

            // erase by swap-pop to avoid O(n)
            _activeTimelines[i] = std::move(_activeTimelines.back());
            _activeTimelines.pop_back();
            continue;  // OK: i stays same to process swapped-in element
        }

        const auto& timed = itScript->second;

        // If commands complete, do NOT spin; just move on.
        if (tl.nextIndex >= timed.size()) {
            if (!tl.commandsComplete) {
                tl.commandsComplete = true;
                spdlog::info("[FB] Timeline: COMPLETE (waiting PairEnd) actor=0x{:08X} scriptKey='{}' elapsed={}",
                             tl.event.actor.formID, tl.scriptKey, tl.elapsed);
            }
            ++i;  // CRITICAL: advance!
            continue;
        }

        // Fire due commands (must advance nextIndex!)
        while (tl.nextIndex < timed.size() && tl.elapsed >= timed[tl.nextIndex].time) {
            const auto& cmd = timed[tl.nextIndex].command;

            spdlog::info(
                "[FB] Timeline: FIRE actor=0x{:08X} scriptKey='{}' t={} elapsed={} idx={}/{} type={} opcode='{}'",
                tl.event.actor.formID, tl.scriptKey, timed[tl.nextIndex].time, tl.elapsed, tl.nextIndex + 1,
                timed.size(), static_cast<std::uint32_t>(cmd.type), cmd.opcode);

            CaptureOriginalScaleIfNeeded(tl, cmd);
            FB::Exec::Execute_MainThread(cmd, tl.event);


            ++tl.nextIndex;  // CRITICAL: progress!
        }

        ++i;  // CRITICAL: advance!
    }
}
