#pragma once
#include "FBStructs.h"

namespace FB::Exec {
    void Execute(const FBCommand& cmd, const FBEvent& ctxEvent);
    void Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent);
}
