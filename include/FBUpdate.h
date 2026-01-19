#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

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

class FBUpdatePump
{
public:
    explicit FBUpdatePump(FBUpdate& update);
    ~FBUpdatePump();

    FBUpdatePump(const FBUpdatePump&) = delete;
    FBUpdatePump& operator=(const FBUpdatePump&) = delete;

    void Start();
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept { return _running.load(); }
    void SetTickHz(double hz);

private:
    void ThreadMain();

    FBUpdate& _update;

    std::atomic<bool> _running{false};
    std::thread _thread;

    std::atomic<double> _tickHz{60.0};
};