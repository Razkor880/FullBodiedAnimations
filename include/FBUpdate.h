#pragma once

#include <atomic>
#include <memory>

class FBConfig;
class FBEvents;

class FBUpdate {
public:
    FBUpdate(FBConfig& config, FBEvents& events);

    void Tick(float dtSeconds);

private:
    FBConfig& _config;
    FBEvents& _events;
};

class FBUpdatePump {
public:
    explicit FBUpdatePump(FBUpdate& update);

    void Start();
    void Stop();

private:
    FBUpdate& _update;
    std::atomic<bool> _running{false};

    // Keep your existing threading mechanism in the .cpp.
    // If you use std::thread or std::jthread, declare it here to match your implementation.
    // Example (uncomment if you use it):
    //
    // std::jthread _thread;
    //
    // or:
    // std::thread _thread;
};
