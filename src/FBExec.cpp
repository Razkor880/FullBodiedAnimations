#include "FBExec.h"
#include "FBMaps.h"

#include <spdlog/spdlog.h>
#include <cstdlib>
#include "FBMorph.h"
#include "FBActors.h"
#include "FBTransform.h"

static bool TryParseFloat(std::string_view s, float& out) {
    // Minimal local helper. (Later we can move parsing utilities to FBUtil.)
    try {
        out = std::stof(std::string(s));
        return true;
    } catch (...) {
        return false;
    }
}


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
static bool TryParseVec3(std::string_view args, float& outX, float& outY, float& outZ) {
          // Parses first 3 comma-separated floats. Extra tokens are ignored.
          // Example: "1, 2, 3, tween=2.0" -> (1,2,3)
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



void FB::Exec::Execute(const FBCommand& cmd, const FBEvent& ctxEvent) {
    if (cmd.type == FBCommandType::Transform && cmd.opcode == "Scale") {
        float scale = 1.0f;

        if (!TryParseFloat(cmd.args, scale)) {
            spdlog::warn("[FB] Exec: failed to parse scale from args='{}'", cmd.args);
            return;
        }



        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        const auto nodeName = FB::Maps::ResolveNode(cmd.target);

        // Safe-any-thread version (queues a task)
        FBTransform::ApplyScale(actor, nodeName, scale);
        return;

    }

    if (cmd.type == FBCommandType::Transform && cmd.opcode == "Move") {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!TryParseVec3(cmd.args, x, y, z)) {
            spdlog::warn("[FB] Exec: failed to parse move vec3 from args='{}'", cmd.args);
            return;
            
        }
        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                          static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
            
        }
        const auto nodeName = FB::Maps::ResolveNode(cmd.target);
        FBTransform::ApplyTranslate(actor, nodeName, x, y, z);
        return;
        
    }
    


    if (cmd.type == FBCommandType::Morph && cmd.opcode == "Set") {
        float value = 0.0f;
        if (!TryParseFloat(cmd.args, value)) {
            spdlog::warn("[FB] Exec: failed to parse morph value from args='{}'", cmd.args);
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        const auto morphName = FB::Maps::ResolveMorph(cmd.target);

        // Safe-any-thread version (queues a task)
        FB::Morph::Set(actor, morphName, value);
        return;
    }


    spdlog::info("[FB] Exec: cmd type {} not implemented (opcode='{}')", static_cast<std::uint32_t>(cmd.type),
                 cmd.opcode);
}


void FB::Exec::Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) {
    // This should be the same logic as Execute(), except it calls _MainThread transform variants.
    if (cmd.type == FBCommandType::Transform && cmd.opcode == "Scale") {
        float scale = 1.0f;
        if (!TryParseFloat(cmd.args, scale)) {
            spdlog::warn("[FB] Exec: failed to parse scale from args='{}'", cmd.args);
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        // Node mapping (pass-through if not found)
        const auto nodeName = FB::Maps::ResolveNode(cmd.target);

        // IMPORTANT: we are already on the game thread in Tick()
        FBTransform::ApplyScale_MainThread(actor, nodeName, scale);
        return;
    }

    if (cmd.type == FBCommandType::Transform && cmd.opcode == "Move") {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!TryParseVec3(cmd.args, x, y, z)) {
            spdlog::warn("[FB] Exec: failed to parse move vec3 from args='{}'", cmd.args);
            return;
            
        }
        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                          static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
            
        }
        const auto nodeName = FB::Maps::ResolveNode(cmd.target);
        FBTransform::ApplyTranslate_MainThread(actor, nodeName, x, y, z);
        return;
        
    }
    

    if (cmd.type == FBCommandType::Morph && cmd.opcode == "Set") {
        float value = 0.0f;
        if (!TryParseFloat(cmd.args, value)) {
            spdlog::warn("[FB] Exec: failed to parse morph value from args='{}'", cmd.args);
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        const auto morphName = FB::Maps::ResolveMorph(cmd.target);

        spdlog::info("[FB] Exec: MORPH Set actor=0x{:08X} role={} morph='{}' value={}", actor->formID,
                     static_cast<std::uint32_t>(cmd.role), morphName, value);

        // Defer Papyrus to task queue even from Tick, to avoid VM timing/reentrancy CTDs.
        FB::Morph::Set(actor, morphName, value);
        return;
    }




    spdlog::info("[FB] Exec: cmd type {} not implemented (opcode='{}')", static_cast<std::uint32_t>(cmd.type),
                 cmd.opcode);
}
