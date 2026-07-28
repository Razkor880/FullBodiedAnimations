#include "FBCommandExecutor.h"
#include "FBExec.h"
#include "FBMaps.h"
#include "FBMorph.h"
#include "FBTransform.h"
#include "FBActors.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <memory>

namespace FB::Exec {
    // ===== Transform Command Executor =====
    class TransformExecutor : public ICommandExecutor {
    public:
        bool Execute(const FBCommand& cmd, const FBEvent& ctxEvent) override;
        bool Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) override;

    private:
        bool ExecuteScale(const FBCommand& cmd, const FBEvent& ctxEvent);
        bool ExecuteMove(const FBCommand& cmd, const FBEvent& ctxEvent);
        static bool TryParseFloat(std::string_view s, float& out);
        static bool TryParseVec3(std::string_view args, float& outX, float& outY, float& outZ);
    };

    // ===== Morph Command Executor =====
    class MorphExecutor : public ICommandExecutor {
    public:
        bool Execute(const FBCommand& cmd, const FBEvent& ctxEvent) override;
        bool Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) override;

    private:
        static bool TryParseFloat(std::string_view s, float& out);
    };

    // ===== Fx/SFX Command Executor =====
    class FxExecutor : public ICommandExecutor {
    public:
        bool Execute(const FBCommand& cmd, const FBEvent& ctxEvent) override;
    };

    // ===== State Command Executor =====
    class StateExecutor : public ICommandExecutor {
    public:
        bool Execute(const FBCommand& cmd, const FBEvent& ctxEvent) override;
    };

    // ===== Helper Functions =====
    static bool TryParseFloatToken(std::string_view s, float& out) {
        // Allows tokens like "x=1.0" or "  1.0"
        if (auto pos = s.find('='); pos != std::string_view::npos) {
            s = s.substr(pos + 1);
        }
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        std::string tmp(s);
        char* end = nullptr;
        out = std::strtof(tmp.c_str(), &end);
        return end != tmp.c_str();
    }

    // ===== Implementation: TransformExecutor =====
    bool TransformExecutor::TryParseFloat(std::string_view s, float& out) {
        try {
            out = std::stof(std::string(s));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool TransformExecutor::TryParseVec3(std::string_view args, float& outX, float& outY, float& outZ) {
        float vals[3]{};
        int count = 0;
        while (count < 3) {
            auto comma = args.find(',');
            std::string_view tok = (comma == std::string_view::npos) ? args : args.substr(0, comma);
            float v = 0.0f;
            if (!TryParseFloatToken(tok, v)) {
                return false;
            }
            vals[count++] = v;
            if (comma == std::string_view::npos) {
                break;
            }
            args = args.substr(comma + 1);
        }
        if (count != 3) {
            return false;
        }
        outX = vals[0];
        outY = vals[1];
        outZ = vals[2];
        return true;
    }

    bool TransformExecutor::Execute(const FBCommand& cmd, const FBEvent& ctxEvent) {
        if (cmd.opcode == "Scale") {
            return ExecuteScale(cmd, ctxEvent);
        } else if (cmd.opcode == "Move") {
            return ExecuteMove(cmd, ctxEvent);
        }
        spdlog::warn("[FB] Exec: Transform opcode '{}' not supported", cmd.opcode);
        return false;
    }

    bool TransformExecutor::Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) {
        if (cmd.opcode == "Scale") {
            float scale = 1.0f;
            if (!TryParseFloat(cmd.args, scale)) {
                spdlog::warn("[FB] Exec: failed to parse scale from args='{}'", cmd.args);
                return false;
            }
            RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
            if (!actor) {
                spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                             static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
                return false;
            }
            const auto nodeName = FB::Maps::ResolveNode(cmd.target);
            FBTransform::ApplyScale_MainThread(actor, nodeName, scale);
            return true;
        } else if (cmd.opcode == "Move") {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!TryParseVec3(cmd.args, x, y, z)) {
                spdlog::warn("[FB] Exec: failed to parse move vec3 from args='{}'", cmd.args);
                return false;
            }
            RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
            if (!actor) {
                return false;
            }
            const auto nodeName = FB::Maps::ResolveNode(cmd.target);
            FBTransform::ApplyTranslate_MainThread(actor, nodeName, x, y, z);
            return true;
        }
        return false;
    }

    bool TransformExecutor::ExecuteScale(const FBCommand& cmd, const FBEvent& ctxEvent) {
        float scale = 1.0f;
        if (!TryParseFloat(cmd.args, scale)) {
            spdlog::warn("[FB] Exec: failed to parse scale from args='{}'", cmd.args);
            return false;
        }
        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return false;
        }
        const auto nodeName = FB::Maps::ResolveNode(cmd.target);
        FBTransform::ApplyScale(actor, nodeName, scale);
        return true;
    }

    bool TransformExecutor::ExecuteMove(const FBCommand& cmd, const FBEvent& ctxEvent) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!TryParseVec3(cmd.args, x, y, z)) {
            spdlog::warn("[FB] Exec: failed to parse move vec3 from args='{}'", cmd.args);
            return false;
        }
        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            return false;
        }
        const auto nodeName = FB::Maps::ResolveNode(cmd.target);
        FBTransform::ApplyTranslate(actor, nodeName, x, y, z);
        return true;
    }

    // ===== Implementation: MorphExecutor =====
    bool MorphExecutor::TryParseFloat(std::string_view s, float& out) {
        try {
            out = std::stof(std::string(s));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool MorphExecutor::Execute(const FBCommand& cmd, const FBEvent& ctxEvent) {
        if (cmd.opcode != "Set") {
            spdlog::warn("[FB] Exec: Morph opcode '{}' not supported", cmd.opcode);
            return false;
        }

        float value = 0.0f;
        if (!TryParseFloat(cmd.args, value)) {
            spdlog::warn("[FB] Exec: failed to parse morph value from args='{}'", cmd.args);
            return false;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            return false;
        }

        FB::Morph::Set(actor, cmd.target, value);
        return true;
    }

    bool MorphExecutor::Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) {
        if (cmd.opcode != "Set") {
            return false;
        }

        float value = 0.0f;
        if (!TryParseFloat(cmd.args, value)) {
            return false;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            return false;
        }

        FB::Morph::Set_MainThread(actor, cmd.target, value);
        return true;
    }

    // ===== Implementation: FxExecutor =====
    bool FxExecutor::Execute(const FBCommand& cmd, const FBEvent& ctxEvent) {
        if (cmd.opcode != "Play") {
            spdlog::warn("[FB] Exec: Fx opcode '{}' not supported", cmd.opcode);
            return false;
        }
        // TODO: Implement FX playback when FBFx is fully implemented
        spdlog::debug("[FB] Exec: Fx.Play deferred (not yet implemented)");
        return true;  // Soft fail: log but don't error
    }

    // ===== Implementation: StateExecutor =====
    bool StateExecutor::Execute(const FBCommand& cmd, const FBEvent& ctxEvent) {
        // TODO: Implement state machine when FBState is fully implemented
        spdlog::debug("[FB] Exec: State command deferred (not yet implemented)");
        return true;  // Soft fail
    }

    // ===== Executor Registry =====
    static std::unordered_map<std::uint8_t, ICommandExecutor*> g_executorRegistry;
    static TransformExecutor g_transformExecutor;
    static MorphExecutor g_morphExecutor;
    static FxExecutor g_fxExecutor;
    static StateExecutor g_stateExecutor;

    void InitializeExecutors() {
        g_executorRegistry[static_cast<std::uint8_t>(FBCommandType::Transform)] = &g_transformExecutor;
        g_executorRegistry[static_cast<std::uint8_t>(FBCommandType::Morph)] = &g_morphExecutor;
        g_executorRegistry[static_cast<std::uint8_t>(FBCommandType::Fx)] = &g_fxExecutor;
        g_executorRegistry[static_cast<std::uint8_t>(FBCommandType::State)] = &g_stateExecutor;
    }

    ICommandExecutor* GetExecutor(FBCommandType type) {
        auto it = g_executorRegistry.find(static_cast<std::uint8_t>(type));
        return it != g_executorRegistry.end() ? it->second : nullptr;
    }

    void RegisterExecutor(FBCommandType type, ICommandExecutor* executor) {
        if (executor) {
            g_executorRegistry[static_cast<std::uint8_t>(type)] = executor;
        }
    }
}
