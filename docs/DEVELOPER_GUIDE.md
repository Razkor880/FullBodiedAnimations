# Full Bodied Animations - Developer Guide

## Building the Plugin

### Prerequisites
- Visual Studio 2022 (Community edition OK)
- CMake 3.21+
- vcpkg (configured with VCPKG_ROOT environment variable)
- Skyrim Special Edition (for testing)

### Build Steps

```bash
# Configure with debug preset
cmake --preset debug

# Build
cmake --build --preset debug

# Output: HelloWorld.dll in build/Debug/
```

### Deploying to Skyrim
```bash
# Set environment variable (one time)
set SKYRIM_FOLDER=C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition

# Build output will auto-copy to SKSE/Plugins/
```

## Adding a New Command Type

### Step 1: Define Command Type (FBStructs.h)
Already defined in `FBCommandType` enum. Pick or add:
- `Transform` (Scale, Move)
- `Morph` (RaceMenu)
- `Fx` (Sound effects)
- `State` (Gameplay effects)

### Step 2: Implement Executor

Create `FBMyExecutor.h`:
```cpp
#pragma once
#include "FBCommandExecutor.h"

class MyExecutor : public FB::Exec::ICommandExecutor {
public:
    bool Execute(const FBCommand& cmd, const FBEvent& ctxEvent) override;
    bool Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) override;
};
```

Create `FBMyExecutor.cpp`:
```cpp
#include "FBMyExecutor.h"
#include "FBActors.h"
#include <spdlog/spdlog.h>

bool MyExecutor::Execute(const FBCommand& cmd, const FBEvent& ctxEvent) {
    // Validate arguments
    if (cmd.target.empty()) {
        spdlog::warn("[FB] MyExecutor: missing target");
        return false;
    }

    // Resolve actor
    RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
    if (!actor) {
        spdlog::info("[FB] MyExecutor: actor not resolved");
        return false;
    }

    // Parse command arguments
    // Example: FBMyCommand_Foo(123, "bar", baz=45)
    
    // Execute (or queue via SKSE task interface)
    spdlog::info("[FB] MyExecutor: executing target='{}' args='{}'", 
                 cmd.target, cmd.args);
    
    return true;  // true = success, false = failure (logged)
}

bool MyExecutor::Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) {
    // Only override if needed for reset/cleanup on main thread
    return false;
}
```

### Step 3: Register Executor

In plugin initialization (e.g., `FBPlugin.cpp`):
```cpp
static MyExecutor g_myExecutor;

// During plugin load:
FB::Exec::RegisterExecutor(FBCommandType::MyType, &g_myExecutor);
```

### Step 4: Use in INI

```ini
[FB:my_animation|Caster]
0.0 FBMyCommand_Foo(arg1,arg2)
1.0 FBMyCommand_Bar(arg3)
```

### Step 5: Testing Checklist
- [ ] Command parses without errors
- [ ] Executor receives correct arguments
- [ ] Effect applies to correct actor (Caster/Target)
- [ ] Timeline completes without crashing
- [ ] Reset phase works (if effect needs cleanup)

---

## Adding a New Integration (e.g., Devourment)

### Step 1: Create Integration Module

Create `FBIntegrationDevourment.h`:
```cpp
#pragma once

namespace FB::Integration::Devourment {
    // Initialize listener for Devourment events
    bool Initialize();
    
    // Shutdown listener
    void Shutdown();
}
```

Create `FBIntegrationDevourment.cpp`:
```cpp
#include "FBIntegrationDevourment.h"
#include "FBEvents.h"
#include <spdlog/spdlog.h>

namespace FB::Integration::Devourment {
    static bool g_initialized = false;

    bool Initialize() {
        spdlog::info("[FB][DVT] Integration initializing");
        
        // TODO: Hook into Devourment event system
        // Listen for Devourment_onSwallow events
        // When received, normalize to FBEvent and push to g_events
        
        g_initialized = true;
        spdlog::info("[FB][DVT] Integration initialized");
        return true;
    }

    void Shutdown() {
        if (!g_initialized) return;
        // TODO: Unregister listeners
        g_initialized = false;
        spdlog::info("[FB][DVT] Integration shutdown");
    }
    
    // Internal: Handle raw Devourment event
    void OnDevourmentSwallow(std::uint32_t predFormID, std::uint32_t preyFormID, 
                            bool isEndo, int locus) {
        // 1) Normalize to config-facing key
        std::string normalizedKey;
        if (!isEndo && locus == 0) {
            normalizedKey = "SwallowSuccess_Stomach";
        } else {
            spdlog::debug("[FB][DVT] Unsupported case: endo={} locus={}", isEndo, locus);
            return;
        }

        // 2) Look up mapping in config
        auto snap = SnapshotManager::GetInstance().GetSnapshot();
        if (!snap) return;
        
        auto it = snap->devourmentEventMap.find(normalizedKey);
        if (it == snap->devourmentEventMap.end()) {
            spdlog::info("[FB][DVT] No mapping for key='{}'", normalizedKey);
            return;
        }
        
        // 3) Create FBEvent and push
        FBEvent evt;
        evt.tag = "FB:" + it->second;
        evt.actor.formID = predFormID;
        
        auto update = FB::GetUpdate();
        if (update) {
            spdlog::info("[FB][DVT] Enqueuing event pred=0x{:08X} prey=0x{:08X}",
                        predFormID, preyFormID);
        }
    }
}
```

### Step 2: Configure Mapping

In `FullBodiedIni.ini`:
```ini
[DevourmentEventMap]
SwallowSuccess_Stomach=devourment_swallow_oral
SwallowSuccess_Anal=devourment_swallow_anal
```

### Step 3: Register in Config

Update `FBConfig.h` to include:
```cpp
struct Snapshot {
    // ... existing fields ...
    std::unordered_map<std::string, std::string> devourmentEventMap;
};
```

### Step 4: Initialize on Plugin Load

In `FBPlugin.cpp`:
```cpp
if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
    FB::Integration::Devourment::Initialize();
}
```

### Step 5: Testing Checklist
- [ ] Devourment event received by integration
- [ ] Event normalized correctly
- [ ] Config mapping resolved
- [ ] Timeline started with correct predator/prey
- [ ] Animation plays with timeline commands
- [ ] Devourment gameplay not disrupted
- [ ] Save/load doesn't cause duplicate events

---

## Testing INI Parser

### Unit Test Example

```cpp
#include "FBConfigParser.h"

void TestIniParsing() {
    FB::ParseErrorCollector errors;
    FB::Config::IniDocument doc;
    
    // Create test INI
    const char* iniContent = R"(
[General]
EnableTimelines=true
ResetDelay=0.5

[FBFiles]
test=paired_test
)";
    
    // Load from string (or file)
    if (!doc.LoadFromFile("test.ini", errors)) {
        std::cout << "Load failed\n";
        errors.ReportSummary();
        return;
    }
    
    // Verify parsing
    assert(doc.GetBool("General", "EnableTimelines") == true);
    assert(doc.GetFloat("General", "ResetDelay") == 0.5f);
    assert(doc.GetValue("FBFiles", "test") == "paired_test");
    
    std::cout << "Test passed\n";
}
```

---

## Debugging Tips

### Enable Trace-Level Logging

In `FullBodiedIni.ini`:
```ini
[Debug]
LogLevel=Trace
```

All logs will appear in `SKSE/Plugins/FullBodiedLog.log`.

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Timeline not firing | Event tag doesn't match config | Check event tag format (must start with `FB:`) |
| Commands not executing | Script key not found | Verify `FBFiles` and per-anim INI paths |
| Actor null | Caster/Target role mismatch | Use `2_` prefix for Target in Caster section |
| Crash on config reload | Snapshot swap failed | Check INI for syntax errors |
| Morphs not applying | Papyrus bridge unavailable | Ensure FBMorphBridge script compiled |

### Viewing Diagnostics

Call from Papyrus console:
```papyrus
FullBodiedQuestScript.GetSystemDiagnostics()  ; Returns JSON string
```

Or check logs:
```
tail -f SKSE/Plugins/FullBodiedLog.log
```

---

## Code Style & Conventions

### Naming
- `FB::` prefix for namespace
- `PascalCase` for classes and types
- `camelCase` for functions and variables
- `_snake_case` for private members
- `CAPS_SNAKE` for constants

### Logging
```cpp
// Good
spdlog::info("[FB][ModuleName] Operation: actor=0x{:08X} value={}", 
            actor->formID, value);

// Avoid
spdlog::info("it worked");  // No context
```

### Error Handling
```cpp
// Good: Check and log
RE::Actor* actor = ResolveActor(evt);
if (!actor) {
    spdlog::warn("[FB] Actor not found: formID=0x{:08X}", evt.actor.formID);
    return false;
}

// Avoid: Silent failures
if (!actor) return;  // Why did it fail?
```

### Memory
- Use `std::shared_ptr` for config snapshots (thread-safe)
- Use `RE::ActorHandle` for cross-thread actor references
- Avoid raw pointers; use smart pointers or stack-allocated objects

---

## Performance Profiling

### Metrics to Monitor
- Average FBUpdate::Tick() time (should be <1ms)
- Active timeline count (usually 0-1)
- Tween count (usually 0-8)
- Sustain apply frequency (throttled to 10 Hz)

### Using Diagnostic Output
```cpp
// Enable in FBUpdate
if (g_logLevel <= FB::LogLevel::Debug) {
    spdlog::debug("[FB] Perf: tick={:.3f}ms timelines={} tweens={}", 
                  tickTimeMs, activeCount, tweenCount);
}
```

---

## Contributing Guidelines

1. **Fork & Branch**: Create feature branch from main
2. **Code Quality**: Follow style guide above
3. **Testing**: Add unit tests for parser changes
4. **Logging**: Use appropriate levels (not Info for debug output)
5. **Documentation**: Update ARCHITECTURE.md if changing core flow
6. **PR**: Provide clear description of changes and testing performed

---

**Last Updated**: 2024  
**Contact**: See CODE_OF_CONDUCT.md for questions
