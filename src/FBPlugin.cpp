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
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"


static FBConfig g_config;
static FBEvents g_events;
static std::unique_ptr<FBUpdate> g_update;
static std::unique_ptr<FBUpdatePump> g_pump;

namespace {
    FBConfig* g_config_ptr = nullptr;
    bool Papyrus_ReloadConfig(RE::StaticFunctionTag*);
    std::int32_t Papyrus_DrainEvents(RE::StaticFunctionTag*);
    std::int32_t Papyrus_TickOnce(RE::StaticFunctionTag*);

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
        if (!g_config_ptr) {
            spdlog::error("[FB] ReloadConfig called but config ptr is null");
            return false;
        }

        const auto before = g_config_ptr->GetGeneration();
        const bool ok = g_config_ptr->Reload();
        const auto after = g_config_ptr->GetGeneration();

        spdlog::info("[FB] ReloadConfig: ok={} gen {} -> {}", ok, before, after);
        return ok;
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
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLogging();

    spdlog::info("[FB] Plugin loaded");

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
            }

            g_events.OnDataLoaded();  // new
        }

        if (msg->type == SKSE::MessagingInterface::kPostLoadGame || msg->type == SKSE::MessagingInterface::kNewGame) {
            g_events.OnPostLoadOrNewGame();  // new
        }


            
        //    static bool s_pushedTestEvent = false;
        //    if (s_pushedTestEvent) {
        //        return;
        //    }

        //    FBEvent e{};
        //    e.tag = "FB_TestEvent";
        //    e.actor.formID = 0x00000014;

        //    g_events.Push(e);
        //    spdlog::info("[FB] Queued test event (in-game): tag='{}' actorFormID=0x{:08X}", e.tag, e.actor.formID);

        //    s_pushedTestEvent = true;
        //    return;
        //}
    });

    return true;
}
