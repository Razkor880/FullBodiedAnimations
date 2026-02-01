#include "FBExec.h"

#include <spdlog/spdlog.h>

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

        FBTransform::ApplyScale(actor, cmd.target, scale);
        return;
    }

    spdlog::info("[FB] Exec: cmd type {} not implemented (opcode='{}')", static_cast<std::uint32_t>(cmd.type),
                 cmd.opcode);
}
