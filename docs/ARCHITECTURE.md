# Full Bodied Animations - Phase 2 Architecture Documentation

## Overview

This document describes the architecture of the Full Bodied Animations SKSE plugin after Phase 2 (Production Hardening & Extensibility).

## Core Architecture

### Timeline Lifecycle

```
[Event Received]
       ↓
   Idle → Running → Complete → Resetting → Done
       ↑_____________↑
       Reset triggered
       on pair-end
```

**States:**
- **Idle**: Timeline exists but hasn't started
- **Running**: Executing timed commands on schedule
- **Complete**: All commands executed, waiting for pair-end event or timeout
- **Resetting**: Reverting transforms, morphs, and other sustained effects
- **Done**: Terminal state; timeline can be removed

**Transitions:**
- Idle → Running: When animation starts (FB: or FB_PAIR_END: event)
- Running → Complete: When all commands have been executed
- Complete → Resetting: When pair-end event arrives or timeout expires
- Resetting → Done: When all cleanup is complete
- Any → Done: Can jump to Done on error or cleanup

### Command Execution Pipeline

```
INI File
   ↓
[Parse] → FBConfigParser
   ↓
[Build] → Snapshot (generation, eventMap, scripts)
   ↓
[Store] → SnapshotManager (atomic, thread-safe)
   ↓
[Queue] → FBEvents.Push(FBEvent)
   ↓
[Drain] → FBUpdate.Tick() reads events
   ↓
[Match] → eventMap[tag] → scriptKey
   ↓
[Timeline] → ActiveTimeline created/reset
   ↓
[Schedule] → TimedCommand checked every frame (elapsed >= time)
   ↓
[Execute] → ICommandExecutor (Transform/Morph/Fx/State)
   ↓
[Sustain] → Effects maintained until reset phase
```

### Thread Safety Model

#### Configuration Snapshot
- **Reader**: Actors can safely call `SnapshotManager::GetSnapshot()` from any thread
- **Writer**: Config reload uses atomic swap in `SnapshotManager::SetSnapshot()`
- **Pattern**: Lock-free reads via `std::atomic<std::shared_ptr<>>`
- **Guarantee**: Returned snapshot pointer remains valid for duration of use

#### Timelines & Tweens
- **Primary Lock**: None required (owner: FBUpdate on main thread)
- **Access Pattern**: FBUpdate reads timelines every frame (synchronous)
- **Actor References**: Use `RE::ActorHandle` for safe cross-thread references
- **Task Dispatch**: Transform/Morph updates queued via SKSE task interface

#### Events Queue
- **Lock**: `FBEvents` uses `std::mutex` for push/drain
- **Pattern**: Push from animation events, drain in FBUpdate::Tick()

## Module Responsibilities

### FBConfig (Configuration & Reload)
- **Old**: Monolithic INI parsing in `BuildSnapshotFromIni()`
- **New**: Delegated to `FBConfigParser` and `SnapshotBuilder`
- **API**: `FBConfig::LoadInitial()`, `FBConfig::Reload()`
- **Snapshot Access**: Via `SnapshotManager::GetSnapshot()`

### FBConfigParser (INI Parsing)
- **Responsibility**: Parse INI files into `IniDocument`, collect errors
- **Error Reporting**: `ParseErrorCollector` gathers all errors, reports summary
- **Design**: Small, testable functions (no game runtime required)
- **Future**: Can be unit-tested without loading game

### FBUpdate (Tick Loop & State Management)
- **Per-Frame Work**:
  1. Drain events
  2. Create/reset timelines from events
  3. Advance timeline elapsed time
  4. Execute commands where `elapsed >= command.time`
  5. Apply sustain (re-apply morphs/transforms to fight animation overrides)
  6. Handle reset scheduling
- **State Machine**: Enforces valid timeline transitions
- **Error Handling**: Logs and skips invalid commands

### FBExec (Command Dispatch)
- **Old**: Monolithic `Execute()` function with switch statements
- **New**: `ICommandExecutor` interface with concrete implementations
- **Executors**:
  - `TransformExecutor`: Scale, Move operations
  - `MorphExecutor`: RaceMenu morph/expression application
  - `FxExecutor`: Sound effects (not yet implemented)
  - `StateExecutor`: Gameplay state changes (not yet implemented)
- **Registry**: `GetExecutor()` returns appropriate executor for command type
- **Extensibility**: `RegisterExecutor()` allows plugins to add custom executors

### FBTransform & FBMorph (Effect Application)
- **Responsibility**: Unchanged; apply effects to actors
- **Thread Safety**: Queue tasks via SKSE interface for main-thread execution
- **Sustain**: FBUpdate repeatedly calls to fight animation overwrites

### FBTimelineState (State Machine)
- **Formal States**: Enum + transition rules
- **Helper**: `StateToString()` for logging
- **Validation**: `CanTransition()` prevents invalid transitions
- **Integration**: `ActiveTimeline::TransitionTo()` enforces rules

### SnapshotManager (Thread-Safe Config)
- **Pattern**: Atomic `shared_ptr` swap (C++20 feature)
- **Readers**: Lock-free reads from any thread
- **Writers**: Mutex-protected swap during reload
- **Generation**: Automatically incremented on swap
- **Singleton**: `SnapshotManager::GetInstance().GetSnapshot()`

### TimelineManager & TweenManager (O(1) Lookups)
- **Old**: O(n) prefix search in flat maps
- **New**: `formID -> Value` hash map (O(1) average)
- **Timeline Manager**: 1 timeline per actor (current game design)
- **Tween Manager**: Multiple tweens per actor, keyed by channel

## INI Format Specification

### General INI (`FullBodiedIni.ini`)

```ini
[General]
EnableTimelines=true          # bool: enable/disable all timelines
ResetOnPairEnd=true           # bool: apply reset when pair ends
ResetDelay=0.5                # float: seconds before reset (safety delay)
DefaultTweenScale=0.0         # float: default scale tween duration if not specified
DefaultTweenMorph=0.0         # float: default morph tween duration if not specified

[FBFiles]
test=paired_test              # alias=clip_name (without .hkx)
oral=paired_oral_human        # Used to find _variants_<clip> folder

[EventMap]
FBEvent=test                  # event_tag=script_key (maps animation event to timeline)

[Debug]
LogLevel=Info                 # Trace, Debug, Info, Warn, Error, Critical
```

### Per-Animation INI (`FB_<alias>.ini`)

```ini
[FB:paired_test|Caster]
0.0 FBScale_Head(0.8)
0.5 FBMorph_LipCorner(0.5)
1.0 FBScale_Head(1.0,tween=0.5)

[FB:paired_test|Target]
0.2 FBMove_Chest(0,0,0.1)
0.8 FBMorph_Lips(0.0)
```

**Command Format**:
- **Timestamp**: Float (seconds from animation start)
- **Opcode**: `FBScale_<node>`, `FBMove_<node>`, `FBMorph_<name>`, `FBFx_Play(<key>)`, `FBState_<op>`
- **Arguments**: Value, optional tween parameters
- **Role Override**: Prefix with `2_` to apply to Target (default: Caster in Caster section)

**Examples**:
```
0.0 FBScale_Head(1.5)                              # Scale head 1.5x at 0s
1.0 FBScale_Head(1.0,tween=0.5)                    # Tween back to 1.0 over 0.5s starting at 1.0s
2.0 FBMove_Chest(0.1,0,0.05)                       # Move chest +0.1x, +0.05z at 2.0s
0.5 2_FBMorph_Lips(0.8)                            # Apply to Target (not Caster)
3.0 FBFx_Play(SwallowSound)                        # Play sound effect
```

## Error Handling & Diagnostics

### Parse Error Collection
- **Pattern**: `ParseErrorCollector` gathers errors instead of fail-fast
- **Critical**: Errors that prevent loading (file not found, malformed INI structure)
- **Warnings**: Non-critical issues (unknown keys, invalid values)
- **Reporting**: `ReportSummary()` logs all errors at startup

### Example Output
```
[FB] Parse Summary: 3 errors (1 critical)
  [CRITICAL] FullBodiedIni.ini:0  : File not found or cannot be opened (CRITICAL)
  [WARN] FB_test.ini:42 [FB:paired_test|Caster] key='time' : Invalid timestamp 'abc' (expected float)
  [WARN] FB_test.ini:50 [FB:paired_test|Caster] key='' : Command not recognized 'Unknown_Foo'
```

## Logging Levels & Patterns

| Level | Use Case | Pattern |
|-------|----------|---------|
| Trace | Detailed execution flow (disabled by default) | `[FB][TRACE] ...` |
| Debug | State changes, loop conditions | `[FB][DEBUG] Timeline state: Idle → Running` |
| Info  | Key events (timeline start, commands fired) | `[FB] Timeline: FIRE cmd=...` |
| Warn  | Non-critical errors, missing features | `[FB] WARN: Feature not implemented` |
| Error | Failures that prevent execution | `[FB] ERR: Actor not found` |

**Logging Spam Reduction**:
- Routine sustain applications: Debug level
- Command execution details: Debug level (unless explicitly enabled)
- Timeline transitions: Debug level in normal operation
- Only INFO for user-facing events (animation start, completion)

## Extension Points

### Adding a New Command Type

1. **Create Executor**:
   ```cpp
   class MyExecutor : public ICommandExecutor {
       bool Execute(const FBCommand& cmd, const FBEvent& ctxEvent) override;
   };
   ```

2. **Register**:
   ```cpp
   FB::Exec::RegisterExecutor(FBCommandType::Custom, new MyExecutor());
   ```

3. **Use in INI**:
   ```ini
   1.0 FBCustom_SomeOp(arg1,arg2)
   ```

### Adding a New Integration (e.g., Devourment)

1. **Listen for External Event**:
   - Create dedicated integration module (e.g., `FBIntegrationDevourment.cpp`)
   - Hook into mod's event system

2. **Normalize to Timeline**:
   - Convert external event → `FBEvent` with appropriate tag
   - Push to `FBEvents` queue

3. **Map in Config**:
   - Define mapping in `[DevourmentEventMap]` section
   - `eventMap` resolves to script key

4. **Reuse Execution Pipeline**:
   - No changes to core timeline/command execution needed
   - Integration acts as event source only

## Performance Considerations

### Timeline Execution (Per-Frame)
- **Timeline Lookup**: O(1) actor hash map (was O(n) prefix search)
- **Command Scheduling**: O(c) where c = commands-to-fire (typically 0-2 per frame)
- **Tween Update**: O(t) where t = active tweens (typically 0-8)
- **Sustain**: O(m) where m = sustained morphs (throttled to 10 Hz)

### Memory
- Per-timeline: ~2 KB overhead + sustain state
- Per-active-tween: ~128 bytes
- Configuration snapshot: Varies (typically <1 MB for moderate animation library)

### Scalability
- **Concurrent Timelines**: 1 per actor (by design)
- **Concurrent Tweens**: Multiple per actor (limited by frame time)
- **Animations**: Can support ~100+ animations in config (limited by INI parsing time)

## Testing Strategy

### Unit Tests (No Game Runtime)
- `IniDocument::LoadFromFile()` → Parse and validate INI structure
- `SnapshotBuilder::BuildFromGeneralIni()` → Construct snapshots from mock INI
- `FBCommand` parsing → Verify command parsing logic
- `TimelineState` transitions → Validate state machine rules
- `ParseErrorCollector` → Verify error collection and reporting

### Integration Tests (With SKSE)
- Timeline creation from events
- Command execution on actors
- State transitions
- Reset logic
- Tween interpolation

### Manual Testing (In-Game)
- Animation playback with various command types
- Config reload without crashes
- Multiple simultaneous animations
- Effect persistence (sustain) during long animations
- Reset on pair-end event

## Migration Notes for Existing Code

### Old Code Pattern
```cpp
// Before: Monolithic parsing
BuildSnapshotFromIni(snapshot);
std::unordered_map<std::string, ActiveTween> _activeTweens;  // O(n) lookup
```

### New Code Pattern
```cpp
// After: Modularized parsing + optimized lookups
FBConfigParser parser;
auto snapshot = parser.Build(errors);
SnapshotManager::GetInstance().SetSnapshot(snapshot);
FB::TweenManager tweens;  // O(1) lookup by actor
```

### FBUpdate Changes
```cpp
// Before: Direct _activeTweens access
// After: Use TweenManager methods
tweens.AddTween(formID, key, tween);
tweens.GetTweensForActor(formID, outList);
```

---

**Document Version**: 1.0  
**Last Updated**: 2024  
**Status**: Phase 2 Infrastructure Foundation
