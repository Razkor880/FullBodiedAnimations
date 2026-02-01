#pragma once
#include "FBUpdate.h"

class FBUpdate;

class FBUpdatePump {
public:
    explicit FBUpdatePump(FBUpdate& update);

    void Start();
    void Stop();

private:
    FBUpdate& _update;
    std::atomic<bool> _running{false};
};
