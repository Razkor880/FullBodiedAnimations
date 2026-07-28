# Phase 2.5: Integration Checklist

This checklist guides integration of Phase 2 infrastructure into existing core systems (FBConfig, FBUpdate, FBExec).

**Estimated Duration**: 2-3 hours  
**Prerequisites**: All Phase 2 infrastructure complete (17 new files)

---

## Pre-Integration Setup

- [ ] Review ARCHITECTURE.md (system design overview)
- [ ] Review QUICK_REFERENCE.md (usage patterns and gotchas)
- [ ] Backup current FBConfig.cpp, FBUpdate.cpp, FBExec.cpp
- [ ] Create branch for integration work
- [ ] Ensure clean build state before changes

---

## Section 1: FBConfig Integration (30 minutes)

**Objective**: Replace global `g_snapshot` with SnapshotManager for thread-safe config.

### Step 1.1: Migrate Global State
- [ ] Locate `std::shared_ptr<Snapshot> g_snapshot` global (FBPlugin.cpp or FBConfig.h)
- [ ] Remove global `g_snapshot` declaration
- [ ] Add `#include "FBSnapshotManager.h"` to FBConfig.h

**Code Change Reference**:
```cpp
// REMOVE:
std::shared_ptr<Snapshot> g_snapshot;

// ADD to any config access:
auto snap = FB::Config::SnapshotManager::GetInstance().GetSnapshot();
```

### Step 1.2: Integrate Error Collection
- [ ] Locate `BuildSnapshotFromIni()` function in FBConfig.cpp
- [ ] Add `#include "FBCommon.h"` to FBConfig.cpp
- [ ] Create `FB::ParseErrorCollector errors;` at start of parsing
- [ ] Replace all error reporting with `errors.Add()` or `errors.AddError()`
- [ ] Replace `return nullptr;` (fail-fast) with `errors.AddError(); continue;` (collect-all)
- [ ] Add `errors.ReportSummary();` after parsing completes

**Code Change Reference**:
```cpp
// BEFORE:
if (!section) {
    spdlog::error("Missing section {}", name);
    return nullptr;  // Fail immediately
}

// AFTER:
if (!section) {
    errors.AddError("FullBodiedIni.ini", line, "General", name, "Missing section", true);
    // Continue parsing to collect all errors
}

// At end of parsing:
errors.ReportSummary();
```

### Step 1.3: Update LoadInitial()
- [ ] Find `FBConfig::LoadInitial()` function
- [ ] Replace config assignment with SnapshotManager call
- [ ] Verify generation number increments

**Code Change Reference**:
```cpp
// BEFORE:
g_snapshot = BuildSnapshotFromIni(...);

// AFTER:
auto newSnap = BuildSnapshotFromIni(...);
if (newSnap) {
    FB::Config::SnapshotManager::GetInstance().SetSnapshot(newSnap);
}
```

### Step 1.4: Update Reload()
- [ ] Find `FBConfig::Reload()` function (if exists)
- [ ] Replace config reload with atomic swap via SnapshotManager
- [ ] Ensure generation increments

### Step 1.5: Update GetSnapshot()
- [ ] Find any `FBConfig::GetSnapshot()` accessor
- [ ] Replace with delegation to SnapshotManager

**Code Change Reference**:
```cpp
// BEFORE:
std::shared_ptr<Snapshot> FBConfig::GetSnapshot() {
    return g_snapshot;
}

// AFTER:
std::shared_ptr<const Snapshot> FBConfig::GetSnapshot() {
    return FB::Config::SnapshotManager::GetInstance().GetSnapshot();
}
```

### Step 1.6: Verify FBConfig Integration
- [ ] Compile and check for errors
- [ ] Verify all parse errors collected (no silent failures)
- [ ] Check generation counter increments on reload
- [ ] Verify no memory leaks (atomic snapshot lifecycle)

**Test**: Run plugin with debug config load, verify all parsing issues reported at startup.

---

## Section 2: FBUpdate Timeline Integration (1 hour)

**Objective**: Replace O(n) vector scans with O(1) hash maps, state machine transitions.

### Step 2.1: Replace Timeline Vector
- [ ] Locate `std::vector<ActiveTimeline> _activeTimelines` in FBUpdate.h
- [ ] Add `#include "FBTimelineManager.h"` to FBUpdate.h
- [ ] Replace vector with `FB::TimelineManager _timelines;`

**Code Change Reference**:
```cpp
// BEFORE:
std::vector<ActiveTimeline> _activeTimelines;

// AFTER:
FB::TimelineManager _timelines;
```

### Step 2.2: Update Timeline Add/Remove
- [ ] Find code that adds timelines: `_activeTimelines.push_back(...)`
- [ ] Replace with `_timelines.Add(...)`
- [ ] Find code that removes timelines: `.erase()` or similar
- [ ] Replace with `_timelines.Remove(formID)`
- [ ] Find code that clears: `_activeTimelines.clear()`
- [ ] Replace with `_timelines.Clear()`

**Code Change Reference**:
```cpp
// BEFORE:
_activeTimelines.push_back(newTimeline);
_activeTimelines.erase(std::remove_if(...), _activeTimelines.end());

// AFTER:
_timelines.Add(newTimeline);
_timelines.Remove(actorFormID);
```

### Step 2.3: Replace Timeline Lookups
- [ ] Find all `std::find_if()` calls searching for timeline by actor formID
- [ ] Replace with `_timelines.GetTimelineForActor(formID)`

**Code Change Reference**:
```cpp
// BEFORE: O(n) vector search
auto it = std::find_if(_activeTimelines.begin(), _activeTimelines.end(),
    [&](const ActiveTimeline& tl) {
        return tl.event.actor.formID == formID;
    });
if (it != _activeTimelines.end()) {
    // Found
}

// AFTER: O(1) map lookup
auto* tl = _timelines.GetTimelineForActor(formID);
if (tl) {
    // Found
}
```

### Step 2.4: Replace Timeline State Logic
- [ ] Add `#include "FBTimelineState.h"` to FBUpdate.cpp
- [ ] Find all boolean flag assignments:
  - `tl.commandsComplete = true;` → `tl.TransitionTo(TimelineState::Complete)`
  - `tl.resetScheduled = true;` → `tl.TransitionTo(TimelineState::Resetting)`
  - `tl.commandsComplete = false;` → ensure `Idle` or appropriate state
- [ ] Replace direct flag checks with state checks: `if (tl.commandsComplete)` → `if (tl.state == TimelineState::Complete)`

**Code Change Reference**:
```cpp
// BEFORE:
tl.commandsComplete = true;
if (tl.commandsComplete && tl.resetScheduled) {
    // Handle completion and reset
}

// AFTER:
tl.TransitionTo(TimelineState::Complete);
if (tl.state == TimelineState::Complete) {
    tl.TransitionTo(TimelineState::Resetting);
}
```

### Step 2.5: Verify Timeline Integration
- [ ] Compile and check for type errors
- [ ] Verify timeline lifecycle still works
- [ ] Check that state machine transitions occur correctly
- [ ] Profile frame time (should be faster with O(1) lookups)

**Test**: Run simple animation, verify timeline completes normally.

---

## Section 3: FBUpdate Tween Integration (30 minutes)

**Objective**: Replace O(n) tween string searches with O(1) hash maps.

### Step 3.1: Replace Tween Map
- [ ] Locate `std::unordered_map<std::string, ActiveTween> _activeTweens` in FBUpdate.h
- [ ] Add `#include "FBTweenManager.h"` to FBUpdate.h
- [ ] Replace map with `FB::TweenManager _tweens;`

**Code Change Reference**:
```cpp
// BEFORE:
std::unordered_map<std::string, ActiveTween> _activeTweens;

// AFTER:
FB::TweenManager _tweens;
```

### Step 3.2: Update Tween Add
- [ ] Find code that adds tweens: `_activeTweens[key] = tween;`
- [ ] Replace with `_tweens.AddTween(actorFormID, key, tween);`
- [ ] Ensure actor formID and tween key are both passed

**Code Change Reference**:
```cpp
// BEFORE:
std::string key = "0x" + std::to_string(actor.formID) + "|" + channelName;
_activeTweens[key] = newTween;

// AFTER:
_tweens.AddTween(actor.formID, channelName, newTween);
```

### Step 3.3: Update Tween Removal
- [ ] Find code that cancels/removes tweens by actor
- [ ] Replace with `_tweens.RemoveTween(actorFormID, key)` or `_tweens.GetTweensForActor(formID)` loop

**Code Change Reference**:
```cpp
// BEFORE: O(n) string prefix search
std::string prefix = "0x" + std::to_string(actor.formID) + "|";
for (auto it = _activeTweens.begin(); it != _activeTweens.end();) {
    if (it->first.starts_with(prefix)) {
        it = _activeTweens.erase(it);
    } else {
        ++it;
    }
}

// AFTER: O(1) lookup
_tweens.RemoveTweensForActor(actor.formID);
```

### Step 3.4: Update Tween Updates
- [ ] Find tween update loops (per-frame tween processing)
- [ ] Replace with `_tweens.GetTweensForActor(formID, tweens)` + loop

**Code Change Reference**:
```cpp
// BEFORE: Iterate all tweens
for (auto& [key, tween] : _activeTweens) {
    // Only care about tweens for this actor
    if (!key.starts_with(actor_prefix)) continue;
    // Update tween...
}

// AFTER: Get actor's tweens directly
std::vector<FBUpdate::ActiveTween> actor_tweens;
if (_tweens.GetTweensForActor(actor.formID, actor_tweens)) {
    for (auto& tween : actor_tweens) {
        // Update tween...
    }
}
```

### Step 3.5: Verify Tween Integration
- [ ] Compile and check for type errors
- [ ] Verify tweens still update smoothly per frame
- [ ] Check cancellation logic still works
- [ ] Profile frame time (should be faster)

**Test**: Run animation with morphs/scale tweens, verify smooth transitions.

---

## Section 4: Command Execution Refactoring (30 minutes)

**Objective**: Migrate from monolithic switch statement to executor interface.

### Step 4.1: Include Executor Header
- [ ] Add `#include "FBCommandExecutor.h"` to FBUpdate.cpp or FBExec.cpp

### Step 4.2: Locate Command Dispatch
- [ ] Find `FBExec::Execute()` or similar command execution function
- [ ] Identify the monolithic switch statement

**Reference**:
```cpp
// BEFORE:
void FBExec::Execute(const FBCommand& cmd, const FBEvent& evt) {
    if (cmd.type == FBCommandType::Transform && cmd.opcode == "Scale") { ... }
    else if (cmd.type == FBCommandType::Transform && cmd.opcode == "Move") { ... }
    else if (cmd.type == FBCommandType::Morph) { ... }
    // Etc...
}
```

### Step 4.3: Replace with Executor Lookup
- [ ] Replace monolithic dispatch with:
```cpp
auto* executor = FB::Exec::GetExecutor(cmd.type);
if (executor) {
    return executor->Execute(cmd, evt);
} else {
    spdlog::warn("No executor for command type {}", static_cast<int>(cmd.type));
    return false;
}
```

### Step 4.4: Verify Executor Implementation
- [ ] Verify TransformExecutor handles Scale and Move
- [ ] Verify MorphExecutor handles morph commands
- [ ] Verify FxExecutor stub doesn't crash
- [ ] Verify StateExecutor stub doesn't crash
- [ ] Add logging for unimplemented executors

### Step 4.5: Test Command Execution
- [ ] Compile
- [ ] Run animation with Scale commands (TransformExecutor)
- [ ] Run animation with morph commands (MorphExecutor)
- [ ] Verify Fx commands log appropriately (FxExecutor stub)

**Test**: Basic animation should work identically to before.

---

## Section 5: Logging Cleanup (30 minutes)

**Objective**: Reduce info-level spam, move routine operations to debug.

### Step 5.1: Identify Spam
- [ ] Search for `spdlog::info()` calls in FBUpdate.cpp
- [ ] Mark routine operations: sustain applications, tween updates, state transitions
- [ ] Mark important events: animation start, completion, major errors

### Step 5.2: Move Routine to Debug
- [ ] Change sustain application logging from `info()` → `debug()`
- [ ] Change tween tick logging from `info()` → `debug()`
- [ ] Change internal state transitions from `info()` → `debug()`
- [ ] Keep only user-facing events at `info()` level

**Code Change Reference**:
```cpp
// BEFORE:
spdlog::info("Applying sustain for actor {}", actor.formID);

// AFTER:
spdlog::debug("Applying sustain for actor {}", actor.formID);

// BUT KEEP INFO FOR:
spdlog::info("Animation '{}' started", timeline.name);
spdlog::info("Animation '{}' completed", timeline.name);
spdlog::error("Failed to apply morph: actor not found");
```

### Step 5.3: Add Consistent Log Prefixes
- [ ] Add module prefix to all logs: `[FBUpdate]`, `[FBConfig]`, `[Timeline]`
- [ ] Add subsystem suffix for clarity: `[FBUpdate::Tick::Sustain]`, etc.

**Code Change Reference**:
```cpp
// BEFORE:
spdlog::debug("Processing tween");

// AFTER:
spdlog::debug("[FBUpdate::Tick::Tween] Processing tween for actor {}", formID);
```

### Step 5.4: Test Logging Levels
- [ ] Set `FB::g_logLevel = FB::LogLevel::Info;` (production)
  - Should see only animation start/end, errors
  - Sustain should NOT appear
- [ ] Set `FB::g_logLevel = FB::LogLevel::Debug;` (development)
  - Should see all operations including sustain, tweens, state transitions

**Test**: Run animation with both log levels, verify appropriate verbosity.

---

## Section 6: Compilation & Testing (30 minutes)

### Step 6.1: Compile Check
- [ ] Open project in Visual Studio 2022
- [ ] Run full rebuild (clean + build)
- [ ] Fix any compilation errors (likely missing includes, template issues)
- [ ] Verify no warnings for phase 2 code

**Expected Issues**:
- Missing `#include` for new headers (add to FBUpdate.h, FBConfig.h)
- Template instantiation issues (check TweenManager::GetTweensForActor signature)
- Return type mismatches (check snapshot pointer const-ness)

### Step 6.2: Link Check
- [ ] Verify all new .cpp files compile (FBCommon.cpp, FBCommandExecutor.cpp, FBConfigParser.cpp)
- [ ] No undefined reference errors
- [ ] Final .dll links successfully

### Step 6.3: Functional Testing
- [ ] Load plugin in Skyrim
- [ ] Run simple animation (1-2 Scale commands)
- [ ] Verify animation starts and completes
- [ ] Check console for errors (should be none)

**Test Animation**:
```ini
[FullBodied_TestAnim]
EventName=FBTestEvent
ScriptEventKey=FB:test
NumCommands=2
FBScale_Head(1.5,tween=0.5)
FBScale_LeftArm(1.2,tween=0.5)
```

### Step 6.4: Regression Testing
- [ ] Run existing complex animation (multiple commands, morphs)
- [ ] Verify output visually identical to before
- [ ] Check performance (frame time should be same or better)
- [ ] Verify no crashes during long animations

### Step 6.5: Config Reload
- [ ] Change INI file while game running
- [ ] Call config reload function
- [ ] Verify new config loads without crash
- [ ] Verify generation counter increments
- [ ] Check that generation affects animation execution

### Step 6.6: Logging Validation
- [ ] Set log level to Info
- [ ] Run animation, observe logs
- [ ] Verify only important events logged (start, end, errors)
- [ ] Sustain operations should NOT appear

- [ ] Set log level to Debug
- [ ] Run animation, observe logs
- [ ] Verify all operations logged with clear prefixes
- [ ] Check state transitions are logged

---

## Post-Integration Verification

### Code Quality
- [ ] No TODO comments left (or captured in PHASE2_CHECKLIST.md)
- [ ] All new code follows existing style conventions
- [ ] Comments added where logic is non-obvious
- [ ] No dead code left (old monolithic functions removed if not referenced)

### Performance
- [ ] Frame time not increased (timeline/tween lookups now O(1))
- [ ] Memory usage reasonable (atomic snapshots don't leak)
- [ ] No excessive logging in normal operation

### Thread Safety
- [ ] Config reads don't block (snapshot manager lock-free)
- [ ] Config writes use minimal locking
- [ ] No race conditions in ActiveTimeline state transitions

### Error Handling
- [ ] All parse errors collected and reported
- [ ] Non-critical errors don't crash plugin
- [ ] Graceful degradation for unimplemented executors

---

## Rollback Plan (If Issues Found)

1. **Critical Compilation Error**
   - Revert to backup branch
   - Check for missing #include files
   - Verify template parameters correct

2. **Incorrect Behavior**
   - Compare old vs new execution path
   - Check state transitions (print actual states)
   - Verify GetTimelineForActor returns correct timeline
   - Check snapshot pointer validity

3. **Performance Regression**
   - Profile hot path (Tick function)
   - Compare O(1) lookup times vs old O(n)
   - Check for unintended allocations
   - Verify no excessive logging

4. **Thread Safety Issues**
   - Add debug logging to snapshot access
   - Check for concurrent config reload
   - Verify atomic swap is atomic
   - Use thread-safe analysis tools

---

## Sign-Off

- [ ] All integration steps completed
- [ ] Code compiles without warnings
- [ ] Functional tests pass
- [ ] Performance acceptable
- [ ] Logging appropriate for level
- [ ] Ready for code review

**Estimated Timeline**:
- FBConfig Integration: 30 min
- FBUpdate Timeline Integration: 1 hour
- FBUpdate Tween Integration: 30 min
- Command Execution Refactoring: 30 min
- Logging Cleanup: 30 min
- Compilation & Testing: 30 min

**Total: 2.5-3 hours**

---

**Checklist Version**: 1.0  
**Date**: July 2024  
**Status**: Ready for execution when Phase 2 infrastructure complete
