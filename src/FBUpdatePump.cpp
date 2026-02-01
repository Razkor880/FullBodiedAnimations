#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

#include "FBUpdatePump.h"
#include "FBUpdate.h"

FBUpdatePump::FBUpdatePump(FBUpdate& update) : _update(update) {}

void FBUpdatePump::Start() {
    spdlog::info("[FB] UpdatePump: Start() called");

    if (_running.exchange(true)) {
        return;  // already running
    }

    std::thread([this]() {
        using clock = std::chrono::steady_clock;

        auto last = clock::now();
        while (_running.load()) {
            const auto now = clock::now();
            const std::chrono::duration<float> dt = now - last;
            last = now;

            if (auto* task = SKSE::GetTaskInterface(); task) {
                const float dtSeconds = dt.count();
                task->AddTask([this, dtSeconds]() { _update.Tick(dtSeconds); });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 Hz
        }
    }).detach();
}

void FBUpdatePump::Stop() { _running.store(false); }
