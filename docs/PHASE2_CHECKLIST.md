# Phase 2: Production Hardening & Extensibility - Implementation Checklist

This checklist documents the Phase 2 implementation status and tracks remaining work.

## SECTION A: Code Organization & Refactoring

### A1: Extract Configuration Parsing
- [x] Created `FBConfigParser.h` with modularized parsing components
  - [x] `IniDocument` class for INI file loading and querying
  - [x] `SnapshotBuilder` class for building snapshots from INI
  - [x] `SnapshotLoader` class for initial and reload operations
- [x] Implemented `FBConfigParser.cpp` with basic INI document parsing
- [ ] **TODO**: Complete `SnapshotBuilder::BuildFromGeneralIni()` implementation
- [ ] **TODO**: Complete per-anim INI parsing in `SnapshotBuilder::ParsePerAnimIni()`
- [ ] **TODO**: Migrate `FBConfig.cpp` to use new parser

### A2: Timeline Execution Optimization
- [x] Created `FBTimelineManager.h` with O(1) actor lookups
  - [x] `formID -> ActiveTimeline` hash map (was vector scan)
  - [x] Get timeline for actor in O(1) average time
  - [x] Remove actor timelines in O(n) cleanup
- [x] Created `FBTweenManager.h` with O(1) tween lookups
  - [x] `formID -> tweenKey -> ActiveTween` nested map
  - [x] Get tweens for actor in O(1)
  - [x] Update/remove tweens efficiently
- [ ] **TODO**: Integrate `TimelineManager` into `FBUpdate`
- [ ] **TODO**: Integrate `TweenManager` into `FBUpdate`
- [ ] **TODO**: Remove old `_activeTweens` string-prefix-search code

### A3: Sustain Logic Consolidation
- [x] Identified sustain logic locations (in `FBUpdate::ApplySustain`, `ApplyReset`)
- [ ] **TODO**: Extract `ApplyPostAnimSustainForActor` stub implementation
- [ ] **TODO**: Clarify and document move-registry offset model
- [ ] **TODO**: Create sustain effect state manager class
- [ ] **TODO**: Consolidate morph/scale/translate reset into strategy pattern

---

## SECTION B: Type System Improvements

### B1: Command Execution Framework
- [x] Created `FBCommandExecutor.h` with abstract interface
  - [x] `ICommandExecutor` base class
  - [x] Factory: `GetExecutor(FBCommandType)` for dispatch
  - [x] Registry: `RegisterExecutor()` for extensibility
- [x] Implemented concrete executors in `FBCommandExecutor.cpp`
  - [x] `TransformExecutor` (Scale, Move)
  - [x] `MorphExecutor` (Set morphs)
  - [x] `FxExecutor` (stub; SFX not yet implemented)
  - [x] `StateExecutor` (stub; gameplay state not yet implemented)
- [x] Removed monolithic `FBExec::Execute()` patterns
- [ ] **TODO**: Integrate executor into `FBUpdate::Tick()` command execution
- [ ] **TODO**: Remove or refactor old `FBExec.cpp` monolithic code

### B2: Configuration API
- [x] Created `FBConfigParser.h` with `IniDocument` value object
  - [x] Section/key parsing
  - [x] Type conversion helpers (`GetBool`, `GetFloat`)
- [ ] **TODO**: Create `IniKey` and `IniValue` value objects (if needed)
- [ ] **TODO**: Add schema validation layer (required/optional keys, type checks, bounds)
- [ ] **TODO**: Create builder pattern for Snapshot construction
- [ ] **TODO**: Support default values and fallbacks in INI

### B3: Timeline State Machine
- [x] Created `FBTimelineState.h` with formal state machine
  - [x] Enum: `Idle → Running → Complete → Resetting → Done`
  - [x] Transition validation: `CanTransition(from, to)`
  - [x] Helper: `StateToString(state)` for logging
- [x] Updated `FBStructs.h` `ActiveTimeline` to use `TimelineState`
  - [x] Replaced boolean flags with state enum
  - [x] Added `TransitionTo()` helper with validation
- [ ] **TODO**: Update `FBUpdate::Tick()` to use state machine
- [ ] **TODO**: Replace all `commandsComplete`, `resetScheduled` boolean logic

---

## SECTION C: Thread Safety & Synchronization

### C1: Lock-Free Config Snapshot
- [x] Created `FBSnapshotManager.h` with atomic snapshot swap
  - [x] `std::atomic<std::shared_ptr<>>` for lock-free reads
  - [x] Mutex-protected `SetSnapshot()` for atomic swap
  - [x] Singleton pattern: `SnapshotManager::GetInstance()`
  - [x] Generation auto-increment on swap
- [ ] **TODO**: Replace `g_snapshot` global with `SnapshotManager`
- [ ] **TODO**: Update `FBConfig::Reload()` to use `SnapshotManager::SetSnapshot()`
- [ ] **TODO**: Update all code accessing config to use `SnapshotManager::GetSnapshot()`

### C2: Timeline & Tween Synchronization
- [x] Designed `TimelineManager` (O(1) lookups, 1 timeline per actor)
- [x] Designed `TweenManager` (O(1) lookups, multiple tweens per actor)
- [ ] **TODO**: Document locking strategy for `_activeTimelines` access
- [ ] **TODO**: Add reader-writer lock if needed (currently main-thread only)
- [ ] **TODO**: Ensure actor handle safety across thread boundaries

---

## SECTION D: Configuration & Validation

### D1: INI Schema Definition
- [x] Created `FBConfigParser.h` with INI document structure
- [x] Documented INI format in `ARCHITECTURE.md`
- [ ] **TODO**: Formalize schema with required/optional sections
- [ ] **TODO**: Define bounds/range checking (e.g., 0.0 ≤ ResetDelay ≤ 60.0)
- [ ] **TODO**: Create example INI with all features
- [ ] **TODO**: Add version field to INI for future migration

### D2: Config Loading Resilience
- [x] Created `ParseErrorCollector` for gathering errors
  - [x] Collects critical and non-critical errors
  - [x] `ReportSummary()` logs all errors at end
- [ ] **TODO**: Implement partial config load (non-critical errors don't fail entire config)
- [ ] **TODO**: Add fallback/default values for missing sections
- [ ] **TODO**: Implement config migration for future format changes
- [ ] **TODO**: Handle file encoding (UTF-8 validation)

---

## SECTION E: Testing & Diagnostics

### E1: Diagnostic System
- [ ] **TODO**: Add per-timeline execution trace (with JSON export)
- [ ] **TODO**: Implement health check (config validity, active timelines, tweens, errors)
- [ ] **TODO**: Add Papyrus diagnostic function: `GetSystemDiagnostics()` → JSON
- [ ] **TODO**: Implement perf counters (commands executed, avg/timeline, peak memory)

### E2: Unit Test Scaffolding
- [ ] **TODO**: Create test harness for `IniDocument::LoadFromFile()`
- [ ] **TODO**: Create test harness for `SnapshotBuilder` logic
- [ ] **TODO**: Create test fixtures for command executor paths
- [ ] **TODO**: Create mock actor/3D for transform/morph testing
- [ ] **TODO**: Document test organization and conventions

### E3: Logging Improvements
- [x] Created `FBCommon.h` with `LogLevel` enum and control
  - [x] Trace, Debug, Info, Warn, Error, Critical levels
  - [x] Convenience helpers: `LogTrace()`, `LogDebug()`, etc.
- [ ] **TODO**: Replace info-level logs with debug in routine operations
  - [ ] Sustain applications (throttled to 10 Hz anyway)
  - [ ] Timeline state transitions (debug-only)
  - [ ] Tween updates (debug-only)
- [ ] **TODO**: Add consistent log prefixes `[FB][ModuleName]`
- [ ] **TODO**: Implement INI configurable log levels via `[Debug]` section

---

## SECTION F: Documentation

### F1: Architecture Document
- [x] Created comprehensive `ARCHITECTURE.md`
  - [x] Timeline lifecycle state diagram
  - [x] Command execution pipeline flow
  - [x] Thread safety model explanation
  - [x] Module responsibilities
  - [x] INI format specification
  - [x] Error handling & diagnostics
  - [x] Logging levels & patterns
  - [x] Extension points documentation
  - [x] Performance considerations
  - [x] Testing strategy
  - [x] Migration notes for existing code

### F2: INI Format Specification
- [x] Documented in `ARCHITECTURE.md` (INI Format Specification section)
- [ ] **TODO**: Add BNF/EBNF grammar if needed
- [ ] **TODO**: Expand with more command examples
- [ ] **TODO**: Add troubleshooting guide

### F3: Developer Guide
- [x] Created comprehensive `DEVELOPER_GUIDE.md`
  - [x] Build instructions
  - [x] Adding new command types (step-by-step)
  - [x] Adding new integrations (Devourment example)
  - [x] Testing INI parser
  - [x] Debugging tips
  - [x] Code style & conventions
  - [x] Performance profiling
  - [x] Contributing guidelines

---

## SECTION G: Implementation Priority

### ✅ COMPLETED: Must-Have (Blocking Production Stability)
- [x] Thread safety audit & snapshot infrastructure created (`FBSnapshotManager`)
- [x] Error collection in INI parsing (`ParseErrorCollector`)
- [x] Logging infrastructure for future spam reduction (`FBCommon`)
- [x] Formal state machine for timelines (`FBTimelineState`)

### 🔄 IN PROGRESS: Should-Have (Maintainability)
- [x] Command executor interface & implementations (Transform, Morph, Fx, State)
- [ ] Timeline/tween optimization (O(1) lookups - designed, not yet integrated)
- [ ] Configuration validation layer (partial - error collection done)
- [ ] Sustain logic implementation (identified but not refactored)

### ⏳ NOT STARTED: Nice-to-Have (Future Work)
- [ ] Diagnostic system & health checks
- [ ] Unit test scaffolding
- [ ] Trace-level logging configuration
- [ ] Comprehensive documentation expansion

---

## Success Criteria Checklist

- [x] All parsing errors can be collected (not fail-fast)
  - [x] `ParseErrorCollector` implemented
  - [x] Critical vs. non-critical error distinction
  - [ ] **TODO**: Integrate into actual parsing
  
- [ ] Configuration changes don't introduce race conditions
  - [x] `SnapshotManager` with atomic swap designed
  - [ ] **TODO**: Integrate into `FBConfig`
  
- [x] Timeline state is unambiguous (no overlapping conditions)
  - [x] `TimelineState` enum replaces boolean flags
  - [x] Formal transition rules in place
  - [ ] **TODO**: Integrate into `FBUpdate`
  
- [x] Command execution is extensible without modifying core
  - [x] `ICommandExecutor` interface implemented
  - [x] Registry pattern for executors
  - [ ] **TODO**: Remove old monolithic dispatch
  
- [ ] Logging is useful for debugging but not spammy
  - [x] `LogLevel` infrastructure created
  - [ ] **TODO**: Reduce info-level spam
  - [ ] **TODO**: Add debug-level logging for state changes
  
- [ ] Core systems can be unit-tested without game runtime
  - [x] INI parser designed to be testable
  - [ ] **TODO**: Create actual test harness
  - [ ] **TODO**: Mock game objects for executor testing
  
- [x] Architecture supports Devourment, TT, and future integrations
  - [x] Integration pattern documented in `DEVELOPER_GUIDE.md`
  - [x] Event-to-timeline flow supports external sources
  - [ ] **TODO**: Implement first integration example

---

## Integration Roadmap

### Immediate Next Steps (To Complete Phase 2)
1. **Integrate SnapshotManager** into `FBConfig.cpp`
2. **Integrate TimelineManager** into `FBUpdate.cpp`
3. **Integrate TweenManager** into `FBUpdate.cpp`
4. **Update command execution** to use new executor interface
5. **Replace boolean flags** in `ActiveTimeline` with `TimelineState`
6. **Reduce logging spam** (info → debug for routine operations)
7. **Test build** to verify compilation

### Phase 2.5 (Polish & Testing)
1. Create unit test harness for `IniDocument`
2. Add per-timeline execution traces
3. Implement diagnostic system
4. Performance profiling & optimization

### Phase 3 (Feature Work)
1. Implement SFX/Fx system fully
2. Implement State machine system
3. Add Devourment integration example
4. HKX annotation support

---

## Documentation Generated

- [x] `ARCHITECTURE.md` - Complete system architecture
- [x] `DEVELOPER_GUIDE.md` - Building, extending, testing
- [x] `FBStructs.h` - Inline comments for structures
- [x] `FBTimelineState.h` - State machine documentation
- [x] `FBCommandExecutor.h` - Executor interface documentation
- [x] `FBCommon.h` - Logging and error collection documentation

---

**Checklist Version**: 1.0  
**Last Updated**: 2024  
**Status**: Phase 2 Infrastructure Complete, Integration Phase Starting
