#include "FBUpdate.h"

#include <RE/Skyrim.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "FBActors.h"
#include "FBConfig.h"
#include "FBEvents.h"
#include "FBExec.h"
#include "FBMaps.h"
#include "FBMorph.h"
#include "FBStructs.h"
#include "FBTransform.h"

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

    // Resolve friendly key -> actual node name (pass-through if already a real name)
    const std::string_view resolvedNode = FB::Maps::ResolveNode(cmd.target);

    // Build key (role + resolved node name)
    auto key = MakeRoleNodeKey(cmd.role, resolvedNode);
    if (tl.originalScale.find(key) != tl.originalScale.end()) {
        return;  // already captured
    }

    // Resolve actor for the role
    RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, cmd.role);
    if (!actor) {
        return;
    }

    // Read current scale from node (use resolved name)
    float current = 1.0f;
    if (!FBTransform::TryGetScale(actor, resolvedNode, current)) {
        spdlog::debug("[FB] Reset: capture failed actor=0x{:08X} role={} key='{}' resolved='{}'", actor->formID,
                      (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode);
        return;
    }

    tl.originalScale.emplace(std::move(key), current);

    spdlog::info("[FB] Reset: captured actor=0x{:08X} role={} key='{}' node='{}' scale={}", actor->formID,
                 (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode, current);
}

static void ApplyReset(const ActiveTimeline& tl) {
    // 1) Restore captured scales
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

        FBTransform::ApplyScale_MainThread(actor, nodeName, original);

        spdlog::info("[FB] Reset: applied actor=0x{:08X} role={} node='{}' scale={}", actor->formID,
                     (roleChar == 'T' ? "T" : "C"), nodeName, original);
    }

    // 2) Clear morphs once per role
    auto ClearRoleMorphs = [&](ActorRole role, const char* roleLabel, const std::unordered_set<std::string>& morphs) {
        if (morphs.empty()) {
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, role);
        if (!actor) {
            return;
        }

        // Guard for now; note actor might unload before task executes, so FB::Morph::Clear should re-check too.
        if (!actor->Get3D1(false)) {
            spdlog::info("[FB] Reset: skip morph clear (3D not loaded) actor=0x{:08X} role={}", actor->formID,
                         roleLabel);
            return;
        }

        for (const auto& m : morphs) {
            FB::Morph::Clear(actor, m);  // queued wrapper
        }

        spdlog::info("[FB] Reset: queued morph clears actor=0x{:08X} role={} count={}", actor->formID, roleLabel,
                     morphs.size());
    };

    ClearRoleMorphs(ActorRole::Caster, "C", tl.touchedMorphsCaster);
    ClearRoleMorphs(ActorRole::Target, "T", tl.touchedMorphsTarget);
}



void FBUpdate::Tick(float dtSeconds) {
    const auto snap = _config.GetSnapshot();
    if (!snap) {
        spdlog::warn("[FB] Tick(dt={}): no config snapshot", dtSeconds);
        return;
    }

    if (_lastSeenGeneration != snap->generation) {
        spdlog::info("[FB] Generation change {} -> {}; dropping {} timelines", _lastSeenGeneration, snap->generation,
                     _activeTimelines.size());

        // Generation drop is effectively an immediate teardown; keep this immediate
        // (you can delay this later if you want, but it’s usually better to reset now).
        if (snap->ResetOnPairEnd) {
            for (auto& tl : _activeTimelines) {
                ApplyReset(tl);
            }
        }

        _activeTimelines.clear();
        _lastSeenGeneration = snap->generation;
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

        // PairEnd is a clip-end marker: close an existing timeline, do NOT start/reset immediately.
        if (e.tag == "PairEnd") {
            auto it = FindActiveTimelineIter(_activeTimelines, e, scriptKey);
            if (it != _activeTimelines.end()) {
                if (snap->ResetOnPairEnd) {
                    const float delay = snap->ResetDelay;

                    if (delay > 0.0f) {
                        // Schedule reset instead of applying immediately.
                        // Keep the timeline alive until the delayed reset fires in section (3).
                        if (!it->resetScheduled) {
                            it->resetScheduled = true;
                            it->resetAtSeconds = _timeSeconds + static_cast<double>(delay);

                            spdlog::info(
                                "[FB] Timeline: CLOSE (PairEnd) scheduled reset actor=0x{:08X} scriptKey='{}' delay={} "
                                "at={}",
                                e.actor.formID, scriptKey, delay, it->resetAtSeconds);
                        } else {
                            spdlog::info(
                                "[FB] Timeline: CLOSE (PairEnd) reset already scheduled actor=0x{:08X} scriptKey='{}' "
                                "at={}",
                                e.actor.formID, scriptKey, it->resetAtSeconds);
                        }

                        // IMPORTANT: do NOT erase timeline yet.
                        continue;
                    }

                    // No delay: apply immediately (current behavior)
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

            // New: clear any reset schedule state (defensive)
            tl.resetScheduled = false;
            tl.resetAtSeconds = 0.0;
            tl.touchedMorphCaster = false;
            tl.touchedMorphTarget = false;

            _activeTimelines.emplace_back(std::move(tl));
            tl.touchedMorphsCaster.clear();
            tl.touchedMorphsTarget.clear();


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

            // New: on explicit reset/start event, clear pending reset schedule
            findIt->resetScheduled = false;
            findIt->resetAtSeconds = 0.0;


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

        // If PairEnd scheduled a delayed reset, wait for time then reset+drop.
        if (tl.resetScheduled) {
            if (_timeSeconds >= tl.resetAtSeconds) {
                ApplyReset(tl);
                spdlog::info("[FB] Timeline: RESET (delayed) actor=0x{:08X} scriptKey='{}' now={} at={}",
                             tl.event.actor.formID, tl.scriptKey, _timeSeconds, tl.resetAtSeconds);

                // erase by swap-pop to avoid O(n)
                _activeTimelines[i] = std::move(_activeTimelines.back());
                _activeTimelines.pop_back();
                continue;  // i stays same to process swapped-in element
            }

            // Not time yet: do not fire commands; just move on.
            ++i;
            continue;
        }

        // Find script commands for this timeline
        const auto itScript = snap->scripts.find(tl.scriptKey);
        if (itScript == snap->scripts.end()) {
            // Drop timeline if script missing
            if (snap->ResetOnPairEnd) {
                const float delay = snap->ResetDelay;

                if (delay > 0.0f) {
                    // Schedule reset and keep timeline around until it fires
                    if (!tl.resetScheduled) {
                        tl.resetScheduled = true;
                        tl.resetAtSeconds = _timeSeconds + static_cast<double>(delay);

                        spdlog::info(
                            "[FB] Timeline: DROP missing scriptKey='{}' scheduled reset actor=0x{:08X} delay={} at={}",
                            tl.scriptKey, tl.event.actor.formID, delay, tl.resetAtSeconds);
                    }

                    // IMPORTANT: do NOT erase timeline yet; advance i to avoid spin
                    ++i;
                    continue;
                }

                // Immediate reset on drop
                ApplyReset(tl);
            }

            spdlog::info("[FB] Timeline: DROP missing scriptKey='{}'", tl.scriptKey);

            // erase by swap-pop to avoid O(n)
            _activeTimelines[i] = std::move(_activeTimelines.back());
            _activeTimelines.pop_back();
            continue;  // i stays same to process swapped-in element
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

            if (cmd.type == FBCommandType::Morph) {
                const std::string_view morphName = FB::Maps::ResolveMorph(cmd.target);

                if (cmd.role == ActorRole::Caster) {
                    tl.touchedMorphsCaster.emplace(morphName);
                } else if (cmd.role == ActorRole::Target) {
                    tl.touchedMorphsTarget.emplace(morphName);
                }
            }


            if (cmd.type == FBCommandType::Morph) {
                if (cmd.role == ActorRole::Target) {
                    tl.touchedMorphTarget = true;
                } else if (cmd.role == ActorRole::Caster) {
                    tl.touchedMorphCaster = true;
                }
            }

            ++tl.nextIndex;  // CRITICAL: progress!
        }

        ++i;  // CRITICAL: advance!
    }
}
