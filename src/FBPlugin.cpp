#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

#include "FBPlugin.h"
#include "FBConfig.h"
#include "FBEvents.h"
#include "FBActors.h"
#include "FBUpdate.h"
#include "FBUpdatePump.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "FBHotkeys.h"

static FBConfig g_config;
static FBEvents g_events;
static std::unique_ptr<FBUpdate> g_update;
static std::unique_ptr<FBUpdatePump> g_pump;
FBUpdate* FB::GetUpdate() { return g_update.get(); }

namespace {
    FBConfig* g_config_ptr = nullptr;
    bool Papyrus_ReloadConfig(RE::StaticFunctionTag*);
    std::int32_t Papyrus_DrainEvents(RE::StaticFunctionTag*);
    std::int32_t Papyrus_TickOnce(RE::StaticFunctionTag*);

    // IMPORTANT:
    // Do NOT patch all vtables. Some entries in RE::VTABLE_* arrays are not Actor-layout
    // and will crash when you replace vfunc slots.
    //
    // Iterate this ONE value across runs: 0,1,2,... until you get periodic Hook tick logs.
    // If an index crashes at startup, revert and try the next.
    constexpr std::size_t kCharacterVtableIndex = 9;
    // Ultra-aggressive late stomp scheduling.
    // - Schedules a burst of N tasks immediately (same frame, later on the task queue)
    // - Then schedules follow-up bursts across subsequent task hops
    // - Can optionally inject Phase 0 recaptures periodically to fight long-hold drift
    static void ScheduleLateStompBurst(RE::Actor* actor, std::uint8_t phase, std::uint32_t burstCount,
                                       std::uint32_t chains, bool allowPhase0Recapture) {
        if (!actor) {
            return;
        }

        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            return;
        }

        const RE::ActorHandle handle = actor->CreateRefHandle();

        // One task = apply (optionally recapture) + enqueue another burst if chains remain.
        taskInterface->AddTask([handle, phase, burstCount, chains, allowPhase0Recapture]() {
            auto aPtr = handle.get();
            RE::Actor* a = aPtr.get();
            if (!a || !a->Get3D1(false)) {
                return;
            }

            auto* up = FB::GetUpdate();
            if (!up) {
                return;
            }

            // Burst: apply multiple times *inside* a single task (super heavy-handed).
            // This is redundant but it removes timing gaps inside the same callback.
            for (std::uint32_t i = 0; i < burstCount; ++i) {
                // Optional: periodically recapture base so long holds don't "drift" into flicker.
                // We do this rarely (every 8th stomp) so we don't constantly redefine base.
                if (allowPhase0Recapture && (i % 8u) == 0u) {
                    up->ApplyPostAnimSustainForActor(a, 0);  // re-capture base + apply
                }

                up->ApplyPostAnimSustainForActor(a, phase);
            }

            // Chain additional bursts (each one happens later on the task queue).
            if (chains > 1) {
                ScheduleLateStompBurst(a, phase, burstCount, chains - 1, allowPhase0Recapture);
            }
        });
    }

    struct NiAVObject_UpdateWorldData_Hook {
        using Fn = void (*)(RE::NiAVObject*, RE::NiUpdateData*);
        static inline Fn func = nullptr;

        static void thunk(RE::NiAVObject* self, RE::NiUpdateData* data) {
            // Apply offset BEFORE vanilla computes world transforms.
            if (auto* up = FB::GetUpdate()) {
                up->ApplyWorldDataSustainForObject(self);
            }

            if (func) {
                func(self, data);
            }
        }
    };

    struct Actor_UpdateAnimation_Hook {
        static void thunk(RE::Actor* self, float delta) {
            static bool s_once = false;
            if (!s_once) {
                s_once = true;
                spdlog::info("[FB] Hook tick FIRST HIT (pre): self=0x{:08X}", self ? self->formID : 0);
            }

            static std::uint32_t s_count = 0;
            if ((++s_count % 60) == 0) {
                spdlog::info("[FB] Hook tick: UpdateAnimation self=0x{:08X}", self ? self->formID : 0);
            }

            func(self, delta);  // original

            auto* actor = self ? self->As<RE::Actor>() : nullptr;
            if (!actor) {
                return;
            }
            if (auto* up = FB::GetUpdate()) {
                up->ApplyPostAnimSustainForActor(actor, 0);
                up->ApplyPostAnimSustainForActor(actor, 1);
                up->ApplyPostAnimSustainForActor(actor, 2);
            }

            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                const RE::ActorHandle handle = actor->CreateRefHandle();
                taskInterface->AddTask([handle]() {
                    auto aPtr = handle.get();
                    RE::Actor* a = aPtr.get();
                    if (!a) {
                        return;
                    }
                    if (auto* up = FB::GetUpdate()) {
                        up->ApplyPostAnimSustainForActor(a, 2);
                    }
                });
            }

            static std::uint32_t s_lateTickCounter = 0;
            // optional phase 1 (second hit) – safe + helps fight flicker
            // Sustain: task-only, 2 hits (late this frame + next frame) to beat late writers.
            if ((++s_lateTickCounter % 1) == 0) {  // every ~3 UpdateAnimation calls
                if (auto* taskInterface = SKSE::GetTaskInterface()) {
                    const RE::ActorHandle handle = self ? self->CreateRefHandle() : RE::ActorHandle{};
                    taskInterface->AddTask([handle]() {
                        auto aPtr = handle.get();
                        RE::Actor* a = aPtr.get();
                        if (!a) {
                            return;
                        }

                        if (auto* up2 = FB::GetUpdate()) {
                            up2->ApplyPostAnimSustainForActor(a, 1);
                        }
                    });
                }
            }

        }

        static inline REL::Relocation<decltype(thunk)> func;
    };



    struct TESObjectREFR_UpdateAnimation_Hook {
        using Fn = void (*)(RE::TESObjectREFR*, float);
        static inline Fn func = nullptr;

        static void thunk(RE::TESObjectREFR* self, float delta) {
            // 1) vanilla first
            if (func) {
                func(self, delta);
            }

            // 2) actors only
            auto* actor = self ? self->As<RE::Actor>() : nullptr;
            if (!actor || !actor->Get3D1(false)) {
                return;
            }

            auto* up = FB::GetUpdate();
            if (!up) {
                return;
            }

            // 3) immediate multi-phase stomp (same stack, right after vanilla)
            // Phase 0 = capture base for *this* frame + apply
            up->ApplyPostAnimSustainForActor(actor, 0);

            // Phase 1/2 = re-apply using cached base (beats late writers this same update)
            up->ApplyPostAnimSustainForActor(actor, 1);
            up->ApplyPostAnimSustainForActor(actor, 2);

            // 4) chained late stomps (very heavy-handed)
            // If you want “beyond excessive”, crank this number.
            // Start with 12–20. You can try 30–60 if you really want to bully it.
            constexpr std::int32_t kChainDepth = 20;

            const RE::ActorHandle handle = actor->CreateRefHandle();

            // Immediate post-vanilla stomp (on the stack)
            up->ApplyPostAnimSustainForActor(actor, 0);
            up->ApplyPostAnimSustainForActor(actor, 1);
            up->ApplyPostAnimSustainForActor(actor, 2);

            // Ultra aggressive:
            // - Phase 2: lots of bursts, lots of chains, with occasional phase0 recapture
            // - Phase 1: lighter but still present
            ScheduleLateStompBurst(actor, 2, /*burstCount*/ 32, /*chains*/ 20, /*allowPhase0Recapture*/ true);
            ScheduleLateStompBurst(actor, 1, /*burstCount*/ 16, /*chains*/ 10, /*allowPhase0Recapture*/ false);
        }
    };

    
    void InstallHooks() {
        {
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_TESObjectREFR[0]};
            const std::uintptr_t orig = vtbl.write_vfunc(0x7D, &TESObjectREFR_UpdateAnimation_Hook::thunk);

            TESObjectREFR_UpdateAnimation_Hook::func = reinterpret_cast<TESObjectREFR_UpdateAnimation_Hook::Fn>(orig);

            spdlog::info("[FB] Hook: TESObjectREFR::UpdateAnimation vfunc installed (orig=0x{:016X})", orig);
            
        }
        // NEW: NiAVObject::UpdateWorldData (0x30)
        {
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_NiAVObject[0]};
            const std::uintptr_t orig = vtbl.write_vfunc(0x30, &NiAVObject_UpdateWorldData_Hook::thunk);
            NiAVObject_UpdateWorldData_Hook::func = reinterpret_cast<NiAVObject_UpdateWorldData_Hook::Fn>(orig);
            spdlog::info("[FB] Hook: NiAVObject::UpdateWorldData vfunc installed (orig=0x{:016X})", orig);
        }
        
    }


    void SetupLogging() {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        *path /= "FullBodiedLog.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));

        spdlog::set_default_logger(std::move(logger));
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%1] %v");
    }

    bool Papyrus_ReloadConfig(RE::StaticFunctionTag*) {
        spdlog::info("[FB] Papyrus: ReloadConfig() called");

        const bool ok = g_config.Reload();
        if (!ok) {
            spdlog::warn("[FB] Papyrus: ReloadConfig failed; keeping existing snapshot");
            return false;
        }

        spdlog::info("[FB] Papyrus: ReloadConfig ok; gen={}", g_config.GetGeneration());
        return true;
    }

    bool RegisterPapyrus() {
        g_config_ptr = std::addressof(g_config);

        auto* papyrus = SKSE::GetPapyrusInterface();
        if (!papyrus) {
            spdlog::error("[FB] Papyrus interface unavailable");
            return false;
        }

        return papyrus->Register([](RE::BSScript::IVirtualMachine* vm) {
            vm->RegisterFunction("ReloadConfig", "FullBodiedQuestScript", Papyrus_ReloadConfig);
            vm->RegisterFunction("DrainEvents", "FullBodiedQuestScript", Papyrus_DrainEvents);
            vm->RegisterFunction("TickOnce", "FullBodiedQuestScript", Papyrus_TickOnce);
            return true;
        });
    }

    std::int32_t Papyrus_DrainEvents(RE::StaticFunctionTag*) {
        auto drained = g_events.Drain();
        spdlog::info("[FB] DrainEvents: drained count={}", drained.size());

        if (!drained.empty()) {
            const auto& e = drained.front();
            spdlog::info("[FB] DrainEvents: first tag='{}' actorFormID=0x{:08X}", e.tag, e.actor.formID);
        }
        return static_cast<std::int32_t>(drained.size());
    }

    std::int32_t Papyrus_TickOnce(RE::StaticFunctionTag*) {
        if (!g_update) {
            spdlog::error("[FB] TickOnce called but FBUpdate not initialized");
            return 0;
        }

        g_update->Tick(1.0f / 60.0f);
        return 1;
    }
}  // namespace


SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLogging();
    //SKSE::AllocTrampoline(256);
    spdlog::info("[FB] Plugin loaded");
    InstallHooks();

    if (!g_config.LoadInitial()) {
        spdlog::error("[FB] Config LoadInitial failed");
        return false;
    }

    spdlog::info("[FB] Config generation: {}", g_config.GetGeneration());

    g_update = std::make_unique<FBUpdate>(g_config, g_events);
    spdlog::info("[FB] FBUpdate Initialized");

    g_pump = std::make_unique<FBUpdatePump>(*g_update);
    // Removed SetTickHz(): not part of current FBUpdatePump surface.
    g_pump->Start();

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (!msg) {
            return;
        }

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            if (!RegisterPapyrus()) {
                spdlog::error("[FB] Papyrus Registration failed");
            } else {
                spdlog::info("[FB] Papyrus registered: FullBodiedQuestScript.ReloadConfig()");
                g_config.Reload();
            }
            FBHotkeys::Install([]() {
                const bool ok = g_config.Reload();
                spdlog::info("[FB] Hotkey: Reload result={} gen={}", ok, g_config.GetGeneration());
            });


            g_events.OnDataLoaded();  // new
        }

        if (msg->type == SKSE::MessagingInterface::kPostLoadGame || msg->type == SKSE::MessagingInterface::kNewGame) {
          
            g_events.OnPostLoadOrNewGame();  // new
        }

    });

    return true;
}
