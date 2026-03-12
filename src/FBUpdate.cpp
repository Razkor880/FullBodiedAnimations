#include "FBUpdate.h"

#include <RE/Skyrim.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
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
        spdlog::debug("[FB] Reset: capture failed actor=0x{:08X} role={} key='{}' resolved='{}'", actor->formID,
                      (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode);
        return;
    }

    tl.originalScale.emplace(std::move(key), current);

    spdlog::info("[FB] Reset: captured actor=0x{:08X} role={} key='{}' node='{}' scale={}", actor->formID,
                 (cmd.role == ActorRole::Target ? "T" : "C"), cmd.target, resolvedNode, current);
}

static void ApplySustain(ActiveTimeline& tl, float nowSeconds) {
    // Throttle: 10 Hz is usually plenty for “win the tug-of-war” without spamming Papyrus.
    constexpr float kSustainInterval = 0.10f;

    if (nowSeconds < tl.nextSustainAtSeconds) {
        return;
    }
    tl.nextSustainAtSeconds = nowSeconds + kSustainInterval;

    auto applyForRole = [&](ActorRole role, const char* roleLabel,
                            const std::unordered_map<std::string, float>& morphs) {
        if (morphs.empty()) {
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(tl.event, role);
        if (!actor) {
            return;
        }

        // Only try if actor has 3D loaded; avoids pointless calls and reduces risk.
        if (!actor->Get3D1(false)) {
            return;
        }

        // Re-apply each sustained morph/expression
        for (const auto& [name, value] : morphs) {
            FB::Morph::Set(actor, name, value);  // queued wrapper (safer)
        }
    };

    applyForRole(ActorRole::Caster, "C", tl.sustainMorphsCaster);
    applyForRole(ActorRole::Target, "T", tl.sustainMorphsTarget);
}

static float Clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static float ApplyEasing(Easing easing, float t) {
    return t;  // Linear only for now
}

static std::string MakeTweenKey(std::uint32_t formID, ActorRole role, std::string_view channel) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08X|%u|", formID, static_cast<unsigned>(role));
    return std::string(buf) + std::string(channel);
}

static std::string MakeMorphCacheKey(std::uint32_t formID, ActorRole role, std::string_view morphName) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08X|%u|", formID, static_cast<unsigned>(role));
    return std::string(buf) + std::string(morphName);
}

static void CancelTweensForActor(std::unordered_map<std::string, FBUpdate::ActiveTween>& activeTweens,
                                 std::unordered_map<std::string, float>& lastMorphValue, std::uint32_t formID) {
    char prefixBuf[16];
    std::snprintf(prefixBuf, sizeof(prefixBuf), "0x%08X|", formID);
    const std::string prefix(prefixBuf);

    for (auto it = activeTweens.begin(); it != activeTweens.end();) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = activeTweens.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = lastMorphValue.begin(); it != lastMorphValue.end();) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = lastMorphValue.erase(it);
        } else {
            ++it;
        }
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
    tl.touchedMorphsCaster.clear();
    tl.touchedMorphsTarget.clear();

    tl.originalScale.clear();
    tl.nextSustainAtSeconds = 0.0f;
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

        if (snap->ResetOnPairEnd) {
            for (auto& tl : _activeTimelines) {
                ApplySustain(tl, _timeSeconds);
                CancelTweensForActor(_activeTweens, _lastMorphValue, tl.event.actor.formID);
                ApplyReset(tl);
            }
        }

        _activeTimelines.clear();
        _activeTweens.clear();
        _lastMorphValue.clear();
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

                        continue;
                    }

                    CancelTweensForActor(_activeTweens, _lastMorphValue, it->event.actor.formID);
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
            tl.resetScheduled = false;
            tl.resetAtSeconds = 0.0;
            tl.touchedMorphCaster = false;
            tl.touchedMorphTarget = false;

            _activeTimelines.emplace_back(std::move(tl));
            _activeTimelines.back().touchedMorphsCaster.clear();
            _activeTimelines.back().touchedMorphsTarget.clear();

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
        tl.elapsed = _timeSeconds - tl.startTimeSeconds;

        if (tl.resetScheduled) {
            if (_timeSeconds >= tl.resetAtSeconds) {
                CancelTweensForActor(_activeTweens, _lastMorphValue, tl.event.actor.formID);
                ApplyReset(tl);
                spdlog::info("[FB] Timeline: RESET (delayed) actor=0x{:08X} scriptKey='{}' now={} at={}",
                             tl.event.actor.formID, tl.scriptKey, _timeSeconds, tl.resetAtSeconds);

                _activeTimelines[i] = std::move(_activeTimelines.back());
                _activeTimelines.pop_back();
                continue;
            }

            ++i;
            continue;
        }

        const auto itScript = snap->scripts.find(tl.scriptKey);
        if (itScript == snap->scripts.end()) {
            if (snap->ResetOnPairEnd) {
                const float delay = snap->ResetDelay;

                if (delay > 0.0f) {
                    if (!tl.resetScheduled) {
                        tl.resetScheduled = true;
                        tl.resetAtSeconds = _timeSeconds + static_cast<double>(delay);

                        spdlog::info(
                            "[FB] Timeline: DROP missing scriptKey='{}' scheduled reset actor=0x{:08X} delay={} at={}",
                            tl.scriptKey, tl.event.actor.formID, delay, tl.resetAtSeconds);
                    }

                    ++i;
                    continue;
                }

                CancelTweensForActor(_activeTweens, _lastMorphValue, tl.event.actor.formID);
                ApplyReset(tl);
            }

            spdlog::info("[FB] Timeline: DROP missing scriptKey='{}'", tl.scriptKey);

            _activeTimelines[i] = std::move(_activeTimelines.back());
            _activeTimelines.pop_back();
            continue;
        }

        const auto& timed = itScript->second;

        if (tl.nextIndex >= timed.size()) {
            if (!tl.commandsComplete) {
                tl.commandsComplete = true;
                spdlog::info("[FB] Timeline: COMPLETE (waiting PairEnd) actor=0x{:08X} scriptKey='{}' elapsed={}",
                             tl.event.actor.formID, tl.scriptKey, tl.elapsed);
            }
            ++i;
            continue;
        }

        while (tl.nextIndex < timed.size() && tl.elapsed >= timed[tl.nextIndex].time) {
            const auto& cmd = timed[tl.nextIndex].command;

            spdlog::info(
                "[FB] Timeline: FIRE actor=0x{:08X} scriptKey='{}' t={} elapsed={} idx={}/{} type={} opcode='{}'",
                tl.event.actor.formID, tl.scriptKey, timed[tl.nextIndex].time, tl.elapsed, tl.nextIndex + 1,
                timed.size(), static_cast<std::uint32_t>(cmd.type), cmd.opcode);

            CaptureOriginalScaleIfNeeded(tl, cmd);
            bool consumedByTween = false;

            float parsedValue = 0.0f;
            bool parsedValueOK = false;
            {
                char* end = nullptr;
                parsedValue = std::strtof(cmd.args.c_str(), &end);
                parsedValueOK = (end != cmd.args.c_str());
            }

            if (parsedValueOK) {
                if (cmd.type == FBCommandType::Transform && cmd.opcode == "Scale") {
                    float tweenDur = cmd.tween.duration;
                    if (tweenDur <= 0.0f && !cmd.tween.hasTween && snap->DefaultTweenScale > 0.0f) {
                        tweenDur = snap->DefaultTweenScale;
                    }

                    const bool wantsTween = (tweenDur > 0.0f);
                    if (wantsTween) {
                        const std::string_view node = FB::Maps::ResolveNode(cmd.target);

                        ActiveTween tw;
                        tw.event = tl.event;
                        tw.role = cmd.role;
                        tw.type = FBCommandType::Transform;
                        tw.channelKey = "Scale|" + std::string(node);
                        tw.target = std::string(node);
                        tw.startTimeSeconds = _timeSeconds + cmd.tween.delay;
                        tw.durationSeconds = tweenDur;
                        tw.startValue = 1.0f;  // captured later at actual tween start
                        tw.endValue = parsedValue;
                        tw.easing = cmd.tween.easing;
                        tw.generation = snap->generation;
                        tw.startCaptured = false;

                        auto key = MakeTweenKey(tl.event.actor.formID, cmd.role, tw.channelKey);
                        _activeTweens[key] = tw;

                        consumedByTween = true;

                        spdlog::info("[FB] Tween: create scale actor=0x{:08X} role={} node='{}' end={} dur={} delay={}",
                                     tl.event.actor.formID, (cmd.role == ActorRole::Target ? "T" : "C"), node,
                                     parsedValue, tweenDur, cmd.tween.delay);
                    }
                } else if (cmd.type == FBCommandType::Morph) {
                    float tweenDur = cmd.tween.duration;
                    if (tweenDur <= 0.0f && !cmd.tween.hasTween && snap->DefaultTweenMorph > 0.0f) {
                        tweenDur = snap->DefaultTweenMorph;
                    }

                    const std::string morph = std::string(FB::Maps::ResolveMorph(cmd.target));

                    if (tweenDur > 0.0f) {
                        const auto cacheKey = MakeMorphCacheKey(tl.event.actor.formID, cmd.role, morph);

                        ActiveTween tw;
                        tw.event = tl.event;
                        tw.role = cmd.role;
                        tw.type = FBCommandType::Morph;
                        tw.channelKey = "Morph|" + morph;
                        tw.target = morph;
                        tw.startTimeSeconds = _timeSeconds + cmd.tween.delay;
                        tw.durationSeconds = tweenDur;
                        tw.startValue = 0.0f;
                        tw.endValue = parsedValue;
                        tw.easing = cmd.tween.easing;
                        tw.generation = snap->generation;
                        tw.startCaptured = true;

                        auto itCache = _lastMorphValue.find(cacheKey);
                        if (itCache != _lastMorphValue.end()) {
                            tw.startValue = itCache->second;
                        }

                        auto key = MakeTweenKey(tl.event.actor.formID, cmd.role, tw.channelKey);
                        _activeTweens[key] = tw;

                        consumedByTween = true;

                        spdlog::info(
                            "[FB] Tween: create morph actor=0x{:08X} role={} morph='{}' start={} end={} dur={} "
                            "delay={}",
                            tl.event.actor.formID, (cmd.role == ActorRole::Target ? "T" : "C"), morph, tw.startValue,
                            parsedValue, tweenDur, cmd.tween.delay);
                    }
                }
            }

            if (!consumedByTween) {
                FB::Exec::Execute_MainThread(cmd, tl.event);
            }

            if (cmd.type == FBCommandType::Morph) {
                const std::string_view resolvedSV = FB::Maps::ResolveMorph(cmd.target);
                const std::string morphName(resolvedSV);

                float v = 0.0f;
                {
                    std::string tmp(cmd.args);
                    char* end = nullptr;
                    v = std::strtof(tmp.c_str(), &end);
                    if (end == tmp.c_str()) {
                        spdlog::warn("[FB] Sustain: failed to parse morph value args='{}' morph='{}'", cmd.args,
                                     morphName);
                    } else {
                        const auto cacheKey = MakeMorphCacheKey(tl.event.actor.formID, cmd.role, morphName);
                        _lastMorphValue[cacheKey] = v;

                        if (cmd.role == ActorRole::Caster) {
                            tl.sustainMorphsCaster[morphName] = v;
                        } else if (cmd.role == ActorRole::Target) {
                            tl.sustainMorphsTarget[morphName] = v;
                        }
                    }
                }
            }

            ++tl.nextIndex;
        }

        ++i;
    }

    // 4) Evaluate active tweens
    for (auto it = _activeTweens.begin(); it != _activeTweens.end();) {
        auto& tw = it->second;

        if (tw.generation != snap->generation) {
            it = _activeTweens.erase(it);
            continue;
        }

        if (_timeSeconds < tw.startTimeSeconds) {
            ++it;
            continue;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(tw.event, tw.role);
        if (!actor) {
            ++it;
            continue;
        }

        if (tw.type == FBCommandType::Transform && !tw.startCaptured) {
            float s = 1.0f;
            if (FBTransform::TryGetScale(actor, tw.target, s)) {
                tw.startValue = s;
            } else {
                tw.startValue = 1.0f;
            }
            tw.startCaptured = true;
        }

        float t = 1.0f;
        if (tw.durationSeconds > 0.0f) {
            t = Clamp01((_timeSeconds - tw.startTimeSeconds) / tw.durationSeconds);
        }

        const float eased = ApplyEasing(tw.easing, t);
        const float v = Lerp(tw.startValue, tw.endValue, eased);

        if (tw.type == FBCommandType::Transform) {
            FBTransform::ApplyScale_MainThread(actor, tw.target, v);
        } else if (tw.type == FBCommandType::Morph) {
            FB::Morph::Set(actor, tw.target, v);

            const auto cacheKey = MakeMorphCacheKey(tw.event.actor.formID, tw.role, tw.target);
            _lastMorphValue[cacheKey] = v;
        }

        if (t >= 1.0f) {
            it = _activeTweens.erase(it);
        } else {
            ++it;
        }
    }
}