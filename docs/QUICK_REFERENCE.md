# Phase 2 Infrastructure - Quick Reference

## New Classes & Modules at a Glance

### Thread Safety
| Class | Purpose | Key Methods |
|-------|---------|------------|
| `SnapshotManager` | Thread-safe config snapshots (atomic swap) | `GetSnapshot()`, `SetSnapshot()`, `GetGeneration()` |
| `ParseErrorCollector` | Collect parse errors (not fail-fast) | `Add()`, `AddError()`, `ReportSummary()` |

### Command Execution
| Class | Purpose | Key Methods |
|-------|---------|------------|
| `ICommandExecutor` | Abstract command executor | `Execute()`, `Execute_MainThread()` |
| `TransformExecutor` | Handle Scale/Move commands | (implements ICommandExecutor) |
| `MorphExecutor` | Handle morph commands | (implements ICommandExecutor) |
| `FxExecutor` | Handle SFX commands (stub) | (implements ICommandExecutor) |
| `StateExecutor` | Handle state commands (stub) | (implements ICommandExecutor) |

### Configuration Parsing
| Class | Purpose | Key Methods |
|-------|---------|------------|
| `IniDocument` | Load & query INI files | `LoadFromFile()`, `GetSection()`, `GetValue()`, `GetBool()`, `GetFloat()` |
| `SnapshotBuilder` | Build snapshots from INI | `BuildFromGeneralIni()` (static) |

### Timeline & Tween Management
| Class | Purpose | Key Methods |
|-------|---------|------------|
| `TimelineManager` | O(1) timeline lookups by actor | `GetTimelineForActor()`, `Add()`, `Remove()`, `Clear()` |
| `TweenManager` | O(1) tween lookups by actor | `GetTweensForActor()`, `AddTween()`, `RemoveTween()` |

### State Machine
| Class | Purpose | Key Constants |
|-------|---------|------------|
| `TimelineState` | Enum with states: Idle, Running, Complete, Resetting, Done | (use `FB::Timeline::StateToString()` for debugging) |
| `ActiveTimeline` | Updated struct (uses `TimelineState` not booleans) | `state`, `TransitionTo()`, `CanTransitionTo()` |

## Usage Patterns

### Getting Current Config (Thread-Safe)
```cpp
#include "FBSnapshotManager.h"

auto snap = FB::Config::SnapshotManager::GetInstance().GetSnapshot();
if (snap) {
    auto scriptKey = snap->eventMap["FBEvent"];
    auto generation = snap->generation;
}
```

### Executing a Command (Using Executors)
```cpp
#include "FBCommandExecutor.h"

FB::Exec::ICommandExecutor* executor = FB::Exec::GetExecutor(FBCommandType::Transform);
if (executor) {
    bool success = executor->Execute(cmd, event);
}
```

### Managing Timelines (O(1) Lookup)
```cpp
#include "FBTimelineManager.h"

FB::TimelineManager timeline_mgr;
timeline_mgr.Add(newTimeline);

// Get timeline for actor
auto* tl = timeline_mgr.GetTimelineForActor(actorFormID);
if (tl) {
    tl->TransitionTo(TimelineState::Running);
}
```

### Parsing INI Files
```cpp
#include "FBConfigParser.h"

FB::ParseErrorCollector errors;
FB::Config::IniDocument doc;
if (doc.LoadFromFile("Data/FullBodiedIni.ini", errors)) {
    bool enabled = doc.GetBool("General", "EnableTimelines", true);
    float delay = doc.GetFloat("General", "ResetDelay", 0.5f);
}
errors.ReportSummary();  // Print any warnings/errors
```

### Managing Tweens (O(1) Lookup)
```cpp
#include "FBTweenManager.h"

FB::TweenManager tweens;
tweens.AddTween(actorFormID, "Scale|Head", activeTween);

// Later, get all tweens for an actor
std::vector<FBUpdate::ActiveTween> tweens_for_actor;
if (tweens.GetTweensForActor(actorFormID, tweens_for_actor)) {
    // Process tweens...
}
```

### Using Timeline State Machine
```cpp
#include "FBTimelineState.h"

ActiveTimeline tl = {...};
// tl.state starts as Idle

// Transition to Running
if (tl.CanTransitionTo(TimelineState::Running)) {
    tl.TransitionTo(TimelineState::Running);
    spdlog::info("Timeline now: {}", FB::Timeline::StateToString(tl.state));
} else {
    spdlog::error("Invalid transition attempted");
}
```

### Controlling Log Level
```cpp
#include "FBCommon.h"

// Set global log level
FB::g_logLevel = FB::LogLevel::Debug;

// Use convenience functions
FB::LogDebug("This is a debug message");
FB::LogInfo("This is an info message");
```

## Migration Checklist (For Integration into Existing Code)

### Updating FBConfig
```cpp
// OLD:
std::shared_ptr<Snapshot> snap = g_snapshot;  // Shared global

// NEW:
auto snap = FB::Config::SnapshotManager::GetInstance().GetSnapshot();
```

### Updating FBUpdate Tick Loop
```cpp
// OLD:
for (auto& tl : _activeTimelines) {
    // O(n) tween search by string prefix
    for (auto& [tweenKey, tween] : _activeTweens) {
        if (tweenKey.starts_with(prefixForActor)) { ... }
    }
}

// NEW:
FB::TimelineManager timeline_mgr;
FB::TweenManager tween_mgr;

for (std::size_t i = 0; i < _activeTimelines.size(); i++) {
    auto& tl = _activeTimelines[i];
    
    // O(1) tween lookup by actor
    std::vector<FBUpdate::ActiveTween> actor_tweens;
    if (tween_mgr.GetTweensForActor(tl.event.actor.formID, actor_tweens)) {
        // Process tweens...
    }
}
```

### Updating Command Execution
```cpp
// OLD:
if (cmd.type == FBCommandType::Transform && cmd.opcode == "Scale") {
    // Direct scale logic
} else if (cmd.type == FBCommandType::Morph) {
    // Direct morph logic
}

// NEW:
auto executor = FB::Exec::GetExecutor(cmd.type);
if (executor) {
    executor->Execute(cmd, event);
}
```

### Updating Timeline State
```cpp
// OLD:
tl.commandsComplete = true;
tl.resetScheduled = true;
tl.resetAtSeconds = now + delay;

// NEW:
tl.TransitionTo(TimelineState::Complete);
if (delay > 0) {
    tl.resetAtSeconds = now + delay;
    // Transition to Resetting when time reached
    tl.TransitionTo(TimelineState::Resetting);
}
```

## Key Design Principles

### 1. **Thread Safety Without Locks on Read Path**
- Readers never lock; use atomic snapshots
- Writers use minimal locking only during swap
- Actors use handles for cross-thread references

### 2. **O(1) Timeline/Tween Lookups**
- By-actor indexing (1 timeline per actor is a game design constraint)
- Multiple tweens per actor indexed by channel key
- Replaces O(n) string prefix searches

### 3. **Pluggable Command Executors**
- Core doesn't know about specific command types
- New executors added via registry, not code changes
- Each executor can be tested independently

### 4. **Formal State Machine for Timelines**
- States are explicit, transitions are validated
- Replaces error-prone boolean flags
- State string available for logging

### 5. **Error Collection Not Fail-Fast**
- All parse errors collected before report
- Non-critical errors don't stop config load
- Clear summary of all issues at startup

## File Organization

```
include/
  FB*.h                  (headers)
  FBCommon.h            (logging & errors)
  FBTimelineState.h     (state machine)
  FBCommandExecutor.h   (executor interface)
  FBConfigParser.h      (INI parsing)
  FBSnapshotManager.h   (thread-safe config)
  FBTimelineManager.h   (O(1) timeline lookup)
  FBTweenManager.h      (O(1) tween lookup)

src/
  FB*.cpp               (implementations)
  FBCommon.cpp         (logging & errors)
  FBCommandExecutor.cpp (executor implementations)
  FBConfigParser.cpp   (INI parsing)

docs/
  ARCHITECTURE.md       (system design)
  DEVELOPER_GUIDE.md   (building & extending)
  PHASE2_CHECKLIST.md  (status & roadmap)
```

## Common Gotchas

| Issue | Why | Fix |
|-------|-----|-----|
| Snapshot pointer expired | Held across frame, config reloaded | Get new snapshot each frame |
| State transition invalid | Wrong enum state used | Check `CanTransitionTo()` first |
| Actor handle null | Actor unloaded mid-timeline | Use `actor->Get3D1(false)` check |
| Tween O(n) lookup | Old code pattern lingering | Use `TweenManager::GetTweensForActor()` |
| Parse errors silent | Old INI parser didn't collect | Check `ParseErrorCollector::ReportSummary()` |

## Next Steps for Integration

1. **FBConfig Integration** (~30 min)
   - Replace `g_snapshot` with `SnapshotManager`
   - Use `ParseErrorCollector` in parsing
   - Update `Reload()` to use atomic swap

2. **FBUpdate Integration** (~1 hour)
   - Replace `_activeTimelines` vector scan with `TimelineManager`
   - Replace `_activeTweens` string search with `TweenManager`
   - Update command execution to use executor registry
   - Replace boolean flags with `TimelineState`

3. **FBExec Refactoring** (~30 min)
   - Remove monolithic dispatch
   - Use executor registry
   - Clean up duplicate parsing code

4. **Testing & Validation** (~30 min)
   - Build and verify compilation
   - Test with simple animation
   - Verify no performance regression
   - Check log output for spam

**Total Estimated Time**: 2-3 hours for full integration

---

**Quick Reference Version**: 1.0  
**Date**: 2024
