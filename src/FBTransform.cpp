#include "FBTransform.h"

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/spdlog.h>

#include <string>

bool FBTransform::ApplyScale(RE::Actor* actor, std::string_view nodeName, float scale)
{
    if (!actor) {
        spdlog::warn("[FB] Transform.ApplyScale: actor=null");
        return false;
    }

    if (nodeName.empty()) {
        spdlog::warn("[FB] Transform.ApplyScale: nodeName empty");
        return false;
    }

    // Clamp here so both logging and execution use the same sanitized value.
    if (scale < 0.0f) {
        scale = 0.0f;
    }

    // Copy nodeName because we will execute later on the task interface.
    std::string nodeStr(nodeName);

    // Use a handle so the actor can be safely resolved later.
    const RE::ActorHandle handle = actor->CreateRefHandle();

    auto* task = SKSE::GetTaskInterface();
    if (!task) {
        spdlog::error("[FB] Transform.ApplyScale: SKSE task interface is null");
        return false;
    }

    // Queue the actual scene graph edit to the game thread.
    task->AddTask([handle, nodeStr = std::move(nodeStr), scale]() mutable {
        auto aPtr = handle.get();
        RE::Actor* a = aPtr.get();
        if (!a) {
            spdlog::info("[FB] Transform.ApplyScale(task): actor handle resolved to null");
            return;
        }

        // The "old working" approach: ask for the actor's 3D at execution time.
        // This tends to be more reliable than gating earlier.
        auto* root = a->Get3D1(false);
        if (!root) {
            root = a->Get3D1(true);
        }

        if (!root) {
            root = a->Get3D2();
        }

        if (!root) {
            spdlog::info("[FB] Transform.ApplyScale(task): actor 0x{:08X} has no 3D roots (3P/1P/2)", a->formID);
            return;
        }

        const RE::BSFixedString bsName(nodeStr.c_str());
        auto* obj = root->GetObjectByName(bsName);
        if (!obj) {
            spdlog::info("[FB] Transform.ApplyScale(task): node '{}' not found on actor 0x{:08X}", nodeStr, a->formID);
            return;
        }

        obj->local.scale = scale;

        // Intentionally not calling UpdateWorldData here:
        // - In your CommonLib build UpdateWorldData() requires params
        // - The engine typically refreshes transforms naturally
        // If we still need an explicit refresh later, we’ll add it after confirming the correct signature.
        spdlog::info("[FB] Transform.ApplyScale(task): actor 0x{:08X} node='{}' scale={}", a->formID, nodeStr, scale);
    });

    // We queued the task successfully.
    spdlog::info("[FB] Transform.ApplyScale: queued actor=0x{:08X} node='{}' scale={}", actor->formID, nodeName, scale);
    return true;
}
