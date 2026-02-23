#include "FBUpdate.h"

#include <RE/Skyrim.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
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
        spdlog::info("[FB] Reset: capture failed actor=0x{:08X} role={} key='{}' resolved='{}'", actor->formID,
                      (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode);
        return;

    } 

    tl.originalScale.emplace(std::move(key), current);

    spdlog::info("[FB] Reset: captured actor=0x{:08X} role={} key='{}' node='{}' scale={}", actor->formID,
                 (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode, current);

}

static void CaptureOriginalTranslateIfNeeded(ActiveTimeline& tl, const FBCommand& cmd) {
    // Only capture for move transforms
    if (cmd.type != FBCommandType::Transform || cmd.opcode != "Move") {
        return;
    }
    const std::string_view resolvedNode = FB::Maps::ResolveNode(cmd.target);
    auto key = MakeRoleNodeKey(cmd.role, resolvedNode);
    if (tl.originalTranslate.find(key) != tl.originalTranslate.end()) {
        return;  // already captured
    }
    RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, cmd.role);
    if (!actor) {
        return;
    }
    std::array<float, 3> current{0.0f, 0.0f, 0.0f};
    if (!FBTransform::TryGetTranslate(actor, resolvedNode, current)) {
        spdlog::debug("[FB] Reset: capture(move) failed actor=0x{:08X} role={} key='{}' resolved='{}'", actor->formID,
                      (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode);
        return;
    }

    tl.originalTranslate.emplace(std::move(key), current);
    spdlog::info("[FB] Reset: captured(move) actor=0x{:08X} role={} key='{}' node='{}' pos=({}, {}, {})", actor->formID,
                 (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode, current[0], current[1],
                 current[2]);
}

static void ApplySustain(ActiveTimeline& tl, float nowSeconds) {
    constexpr float kMorphSustainInterval = 0.10f;

    auto applyMorphsForRole = [&](ActorRole role, const std::unordered_map<std::string, float>& morphs) {
        if (morphs.empty()) return;
        RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, role);
        if (!actor || !actor->Get3D1(false)) return;
        for (const auto& [name, value] : morphs) {
            FB::Morph::Set(actor, name, value);
        }
    };

    auto applyTranslateForRole = [&](ActorRole role,
                                     const std::unordered_map<std::string, std::array<float, 3>>& moves) {
        if (moves.empty()) return;
        RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, role);
        if (!actor || !actor->Get3D1(false)) return;

        // Apply every tick (no throttle) so we can actually win vs animation.
        for (const auto& [nodeName, vec] : moves) {
            FBTransform::ApplyTranslate_MainThread(actor, nodeName, vec[0], vec[1], vec[2]);
        }
    };

    // Transforms: always (per tick)
    applyTranslateForRole(ActorRole::Caster, tl.sustainTranslateCaster);
    applyTranslateForRole(ActorRole::Target, tl.sustainTranslateTarget);

    // Morphs: throttled
    if (nowSeconds >= tl.nextSustainAtSeconds) {
        tl.nextSustainAtSeconds = nowSeconds + kMorphSustainInterval;
        applyMorphsForRole(ActorRole::Caster, tl.sustainMorphsCaster);
        applyMorphsForRole(ActorRole::Target, tl.sustainMorphsTarget);
    }
}


void FBUpdate::ApplyPostAnimSustainForActor(RE::Actor* actor, std::uint8_t phase) {
    if (!actor) {
        return;
    }
    if (_activeTimelines.empty()) {
        return;
    }

    auto* root = actor->Get3D1(false);
    if (!root) {
        return;
    }

    // Additive sustain:
    // - sustainTranslate* maps store OFFSETS (dx,dy,dz)
    // - in phase 0, capture the animated "base" translate for this frame into sustainTranslateBase*
    // - in later phases, reuse the cached base so we don't double-add if we already applied
    for (auto& tl : _activeTimelines) {
        if (!tl.event.IsValid()) {
            continue;
        }

        auto applyForRole = [&](ActorRole role, const std::unordered_map<std::string, std::array<float, 3>>& offsets,
                                std::unordered_map<std::string, std::array<float, 3>>& lastApplied) {
            if (offsets.empty()) {
                return;
            }

            RE::Actor* resolved = FB::Actors::ResolveActorForEvent(tl.event, role);
            if (!resolved || resolved != actor) {
                return;
            }

            // Tolerance for "current == lastApplied" comparisons.
            // Translate units are small-ish; tweak if needed.
            constexpr float kEps = 0.0005f;

            auto approxEq3 = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
                return std::fabs(a[0] - b[0]) <= kEps && std::fabs(a[1] - b[1]) <= kEps &&
                       std::fabs(a[2] - b[2]) <= kEps;
            };

            for (const auto& [nodeName, off] : offsets) {
                std::array<float, 3> cur{};
                if (!FBTransform::TryGetTranslate(actor, nodeName, cur)) {
                    continue;
                }

                // If the current value looks like what we applied last time, it's probably "base+offset".
                // Recover base by subtracting offset. Otherwise treat current as base (someone overwrote us).
                std::array<float, 3> base = cur;

                auto itLast = lastApplied.find(nodeName);
                if (itLast != lastApplied.end()) {
                    if (approxEq3(cur, itLast->second)) {
                        base[0] = cur[0] - off[0];
                        base[1] = cur[1] - off[1];
                        base[2] = cur[2] - off[2];
                    }
                }

                std::array<float, 3> applied{base[0] + off[0], base[1] + off[1], base[2] + off[2]};

                FBTransform::ApplyTranslate_MainThread(actor, nodeName, applied[0], applied[1], applied[2]);

                // Record what we applied so we can detect double-apply next time.
                lastApplied[nodeName] = applied;
            }
        };

        applyForRole(ActorRole::Caster, tl.sustainTranslateCaster, tl.sustainTranslateLastAppliedCaster);
        applyForRole(ActorRole::Target, tl.sustainTranslateTarget, tl.sustainTranslateLastAppliedTarget);
    }
}





static void ApplyReset(ActiveTimeline& tl) {
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


      // 1b) Restore captured translations
    for (const auto& [key, original] : tl.originalTranslate) {
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
        FBTransform::ApplyTranslate_MainThread(actor, nodeName, original[0], original[1], original[2]);
        spdlog::info("[FB] Reset: applied(move) actor=0x{:08X} role={} node='{}' pos=({}, {}, {})", actor->formID,
                     (roleChar == 'T' ? "T" : "C"), nodeName, original[0], original[1], original[2]);
    }
    






    // 2) Clear sustained morphs (RaceMenu + expressions) once per role
    auto ClearRoleMorphs = [&](ActorRole role, const char* roleLabel,
                               const std::unordered_map<std::string, float>& morphs) {
        if (morphs.empty()) {
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, role);
        if (!actor) {
            return;
        }

        // If actor isn't loaded, we still clear our internal sustain state below.
        // We just skip attempting to push clears to the game.
        if (!actor->Get3D1(false)) {
            spdlog::info("[FB] Reset: skip morph clear (3D not loaded) actor=0x{:08X} role={}", actor->formID,
                         roleLabel);
            return;
        }

        for (const auto& [name, _value] : morphs) {
            FB::Morph::Clear_MainThread(actor, name);  // queued wrapper (safe)
        }

        spdlog::info("[FB] Reset: queued morph clears actor=0x{:08X} role={} count={}", actor->formID, roleLabel,
                     morphs.size());
    };

    ClearRoleMorphs(ActorRole::Caster, "C", tl.sustainMorphsCaster);
    ClearRoleMorphs(ActorRole::Target, "T", tl.sustainMorphsTarget);

    // 3) Clear internal state so sustain stops immediately
    tl.sustainMorphsCaster.clear();
    tl.sustainMorphsTarget.clear();
    tl.touchedMorphsCaster.clear();  // optional: if you still keep these sets around
    tl.touchedMorphsTarget.clear();  // optional
    tl.sustainTranslateCaster.clear();
    tl.sustainTranslateTarget.clear();
    tl.sustainTranslateBaseCaster.clear();
    tl.sustainTranslateBaseTarget.clear();
    tl.sustainTranslateLastAppliedCaster.clear();
    tl.sustainTranslateLastAppliedTarget.clear();

    tl.originalScale.clear();        // optional: avoids carrying stale scale captures
    tl.originalTranslate.clear();    // optional: avoids carrying stale move captures
    tl.nextSustainAtSeconds = 0.0f;  // optional: reset throttle
}



void FBUpdate::Tick(float dtSeconds) {
    const auto snap = _config.GetSnapshot();
    if (!snap) {
        spdlog::warn("[FB] Tick(dt={}): no config snapshot", dtSeconds);
        return;
    }
    ++_postAnimEpoch;

    if (_lastSeenGeneration != snap->generation) {
        spdlog::info("[FB] Generation change {} -> {}; dropping {} timelines", _lastSeenGeneration, snap->generation,
                     _activeTimelines.size());

        // Generation drop is effectively an immediate teardown; keep this immediate
        // (you can delay this later if you want, but it’s usually better to reset now).
        if (snap->ResetOnPairEnd) {
            for (auto& tl : _activeTimelines) {
                ApplySustain(tl, _timeSeconds);

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

        // Policy: 1 active timeline per actor + scriptKey (by formID)
        auto findIt = FindActiveTimelineIter(_activeTimelines, e, scriptKey);


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
        ApplySustain(tl, _timeSeconds);


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
            CaptureOriginalTranslateIfNeeded(tl, cmd);
            FB::Exec::Execute_MainThread(cmd, tl.event);


            if (cmd.type == FBCommandType::Transform && cmd.opcode == "Move") {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                // Use same parsing logic as Exec: reuse std::strtof minimal parse
                // (Keep it local like your Morph sustain block does.)
                {
                    // Parse first three comma-separated floats
                    std::string s = cmd.args;
                    const char* p = s.c_str();
                    char* end = nullptr;

                    x = std::strtof(p, &end);
                    if (end == p) {
                        spdlog::warn("[FB] Sustain: failed to parse move x args='{}' node='{}'", cmd.args, cmd.target);
                    } else {
                        p = end;
                        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
                        y = std::strtof(p, &end);
                        if (end == p) {
                            spdlog::warn("[FB] Sustain: failed to parse move y args='{}' node='{}'", cmd.args,
                                         cmd.target);
                        } else {
                            p = end;
                            while (*p == ',' || *p == ' ' || *p == '\t') ++p;
                            z = std::strtof(p, &end);
                            if (end == p) {
                                spdlog::warn("[FB] Sustain: failed to parse move z args='{}' node='{}'", cmd.args,
                                             cmd.target);
                            } else {
                                const std::string_view resolvedSV = FB::Maps::ResolveNode(cmd.target);
                                const std::string nodeName(resolvedSV);  // owning key

                                const std::array<float, 3> vec{x, y, z};

                                if (cmd.role == ActorRole::Caster) {
                                    tl.sustainTranslateCaster[nodeName] = vec;
                                    tl.sustainTranslateBaseCaster.erase(nodeName);  // if still present
                                    tl.sustainTranslateLastAppliedCaster.erase(nodeName);
                                } else if (cmd.role == ActorRole::Target) {
                                    tl.sustainTranslateTarget[nodeName] = vec;
                                    tl.sustainTranslateBaseTarget.erase(nodeName);  // if still present
                                    tl.sustainTranslateLastAppliedTarget.erase(nodeName);
                                }
                            }
                        }
                    }
                }
            }


            if (cmd.type == FBCommandType::Morph) {
                const std::string_view resolvedSV = FB::Maps::ResolveMorph(cmd.target);
                const std::string morphName(resolvedSV);  // owning key

                // Parse cmd.args as float
                float v = 0.0f;
                {
                    // minimal local parse (no dependency on FBExec helpers)
                    std::string tmp(cmd.args);
                    char* end = nullptr;
                    v = std::strtof(tmp.c_str(), &end);
                    if (end == tmp.c_str()) {
                        spdlog::warn("[FB] Sustain: failed to parse morph value args='{}' morph='{}'", cmd.args,
                                     morphName);
                        // don't record sustain if we can't parse
                    } else {
                        if (cmd.role == ActorRole::Caster) {
                            tl.sustainMorphsCaster[morphName] = v;
                        } else if (cmd.role == ActorRole::Target) {
                            tl.sustainMorphsTarget[morphName] = v;
                        }
                    }
                }
            }




            ++tl.nextIndex;  // CRITICAL: progress!
        }

        ++i;  // CRITICAL: advance!
    }
}
