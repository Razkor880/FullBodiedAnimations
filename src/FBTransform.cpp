#include "FBTransform.h"
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


    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("[FB] Transform.ApplyScale: SKSE task interface is null");
        return false;
    }

    taskInterface->AddTask([handle, nodeStr, scale]() mutable {
        auto aPtr = handle.get();
        RE::Actor* a = aPtr.get();
        if (!a) {
            spdlog::info("[FB] Transform.ApplyScale(task): actor handle resolved to null");
            return;
        }

        auto* root = a->Get3D1(false);
        if (!root) {
            spdlog::info("[FB] Transform.ApplyScale(task): actor 0x{:08X} has no 3P 3D root", a->formID);
            return;
        }


        const RE::BSFixedString bsName(nodeStr.c_str());
        auto* obj = root->GetObjectByName(bsName);
        if (!obj) {
            spdlog::info("[FB] Transform.ApplyScale(task): node '{}' not found on actor 0x{:08X}", nodeStr, a->formID);
            return;
        }
        obj->local.scale = scale;

        //RE::NiUpdateData updateData{};
        //obj->UpdateWorldData(&updateData);

        spdlog::info("[FB] Transform.ApplyScale(task): actor 0x{:08X} node='{}' scale={}", a->formID, nodeStr, scale);
    });

    spdlog::info("[FB] Transform.ApplyScale: queued actor=0x{:08X} node='{}' scale={}", actor->formID, nodeName, scale);
    return true;


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
