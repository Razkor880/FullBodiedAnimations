#include "FBTransform.h"

#include <spdlog/spdlog.h>

#include <string>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

void FBTransform::ApplyScale_MainThread(RE::Actor* actor, std::string_view nodeName, float scale) {
    if (!actor) {
        spdlog::warn("[FB] Transform.ApplyScale_MainThread: actor=null");
        return;
    }

    if (nodeName.empty()) {
        spdlog::warn("[FB] Transform.ApplyScale_MainThread: nodeName empty");
        return;
    }

    // Clamp so both logging and execution use the same sanitized value.
    if (scale < 0.0f) {
        scale = 0.0f;
    }

    auto* root = actor->Get3D1(false);
    if (!root) {
        spdlog::info("[FB] Transform.ApplyScale_MainThread: actor 0x{:08X} has no 3D root", actor->formID);
        return;
    }

    // Use BSFixedString for lookup
    const RE::BSFixedString bsName(std::string(nodeName).c_str());
    auto* obj = root->GetObjectByName(bsName);
    if (!obj) {
        spdlog::info("[FB] Transform.ApplyScale_MainThread: node '{}' not found on actor 0x{:08X}", nodeName,
                     actor->formID);
        return;
    }

    obj->local.scale = scale;

    // If you ever need to force updates, revisit this—leave off for now as you had it.
    // RE::NiUpdateData updateData{};
    // obj->UpdateWorldData(&updateData);

    spdlog::info("[FB] Transform.ApplyScale_MainThread: actor 0x{:08X} node='{}' scale={}", actor->formID, nodeName,
                 scale);
}

void FBTransform::ApplyScale(RE::Actor* actor, std::string_view nodeName, float scale) {
    if (!actor) {
        spdlog::warn("[FB] Transform.ApplyScale: actor=null");
        return;
    }

    if (nodeName.empty()) {
        spdlog::warn("[FB] Transform.ApplyScale: nodeName empty");
        return;
    }

    // Clamp here so both logging and execution use the same sanitized value.
    if (scale < 0.0f) {
        scale = 0.0f;
    }

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("[FB] Transform.ApplyScale: SKSE task interface is null");
        return;
    }

    // Copy nodeName because we will execute later on the task interface.
    const std::string nodeStr(nodeName);

    // Use a handle so the actor can be safely resolved later.
    const RE::ActorHandle handle = actor->CreateRefHandle();

    // IMPORTANT: no move-captures; keep lambda copyable for TaskFn
    taskInterface->AddTask([handle, nodeStr, scale]() {
        auto aPtr = handle.get();
        RE::Actor* a = aPtr.get();
        if (!a) {
            spdlog::info("[FB] Transform.ApplyScale(task): actor handle resolved to null");
            return;
        }

        // Now we are on the game thread, apply immediately:
        FBTransform::ApplyScale_MainThread(a, nodeStr, scale);
    });

    spdlog::info("[FB] Transform.ApplyScale: queued actor=0x{:08X} node='{}' scale={}", actor->formID, nodeName, scale);
}

bool FBTransform::TryGetScale(RE::Actor* actor, std::string_view nodeName, float& outScale) {
    if (!actor || nodeName.empty()) {
        return false;
    }

    auto* root = actor->Get3D1(false);
    if (!root) {
        return false;
    }

    const RE::BSFixedString bsName(std::string(nodeName).c_str());
    auto* obj = root->GetObjectByName(bsName);
    if (!obj) {
        return false;
    }

    outScale = obj->local.scale;
    return true;
}
