#include "FBMorph.h"

#include <RE/Skyrim.h>
#include <RE/F/FunctionArguments.h>
#include <RE/S/SkyrimVM.h>
#include <SKSE/SKSE.h>

#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

namespace
{
    static RE::BSScript::IVirtualMachine* GetVM() {
        if (auto* skyrimVM = RE::SkyrimVM::GetSingleton(); skyrimVM) {
            return skyrimVM->impl ? skyrimVM->impl.get() : nullptr;
        }
        return nullptr;
    }





     static void Papyrus_SetMorph(RE::Actor* actor, const char* morphName, float value) {
        if (!actor || !morphName) {
            return;
        }

        auto* vm = GetVM();
        if (!vm) {
            spdlog::warn("[FB] Morph: VM not available; SetMorph skipped");
            return;
        }

        spdlog::info("[FB] Morph: dispatch {}.{} actor=0x{:08X} morph='{}' value={}", FB::Morph::kBridgeClass,
                     FB::Morph::kFnSetMorph, actor->formID, morphName, value);

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{};

        // FBMorphBridge.FBSetMorph(Actor akActor, String morphName, Float value)
        auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor), RE::BSFixedString(morphName),
                                               static_cast<float>(value));

        const bool ok = vm->DispatchStaticCall(RE::BSFixedString(FB::Morph::kBridgeClass),
                                               RE::BSFixedString(FB::Morph::kFnSetMorph), args, result);

        spdlog::debug("[FB] MorphBridgeCall: {}.{} ok={} morph='{}' value={}", FB::Morph::kBridgeClass,
                      FB::Morph::kFnSetMorph, ok, morphName, value);
    }

    static void Papyrus_ClearMorph(RE::Actor* actor, const char* morphName) {
        if (!actor || !morphName) {
            return;
        }

        auto* vm = GetVM();
        if (!vm) {
            spdlog::warn("[FB] Morph: VM not available; ClearMorph skipped");
            return;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{};

        auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor), RE::BSFixedString(morphName));

        spdlog::info("[FB] Morph: dispatch {}.{} actor=0x{:08X} morph='{}'", FB::Morph::kBridgeClass,
                     FB::Morph::kFnClear, actor->formID, morphName);

        const bool ok = vm->DispatchStaticCall(RE::BSFixedString(FB::Morph::kBridgeClass),
                                               RE::BSFixedString(FB::Morph::kFnClear), args, result);

        spdlog::info("[FB] Morph: dispatch returned ok={}", ok);
    }
}

namespace FB::Morph {
    void Set(RE::Actor* actor, std::string_view morphName, float value) {
        if (!actor || morphName.empty()) {
            return;
        }

        // copy because BSFixedString wants a stable c-string for the dispatch call
        std::string nameCopy(morphName);
        Papyrus_SetMorph(actor, nameCopy.c_str(), value);
    }

    void Clear_MainThread(RE::Actor* actor, std::string_view morphName) {
        if (!actor || morphName.empty()) {
            return;
        }

        std::string nameCopy(morphName);
        Papyrus_ClearMorph(actor, nameCopy.c_str());
    }

    void Clear(RE::Actor* actor, std::string_view morphName) {
        if (!actor || morphName.empty()) {
            return;
        }

        auto* task = SKSE::GetTaskInterface();
        if (!task) {
            spdlog::warn("[FB] Morph.Clear: task interface missing");
            return;
        }

        const RE::ActorHandle handle = actor->CreateRefHandle();
        const std::string nameCopy(morphName);

        task->AddTask([handle, nameCopy]() {
            auto aPtr = handle.get();
            auto* a = aPtr.get();
            if (!a) {
                spdlog::info("[FB] Morph.Clear(task): actor handle resolved to null");
                return;
            }

            // extra safety: actor might unload between queue and execution
            if (!a->Get3D1(false)) {
                spdlog::info("[FB] Morph.Clear(task): skip (3D not loaded) actor=0x{:08X}", a->formID);
                return;
            }

            Clear_MainThread(a, nameCopy);
        });
    }
}
