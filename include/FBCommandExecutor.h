#pragma once

#include "FBStructs.h"

namespace RE {
    class Actor;
}

// Abstract interface for command execution
// Allows new command types to be added without modifying core dispatch logic

namespace FB::Exec {
    class ICommandExecutor {
    public:
        virtual ~ICommandExecutor() = default;

        // Execute a command synchronously (may queue tasks internally)
        // Returns true if execution was successful or was queued
        virtual bool Execute(const FBCommand& cmd, const FBEvent& ctxEvent) = 0;

        // Execute on main thread (used for reset/cleanup)
        // Only implemented for commands that modify game state
        virtual bool Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) {
            return false;  // Most commands don't need main thread variant
        }
    };

    // Factory function to get executor for a command type
    // Returns nullptr if command type is not supported
    ICommandExecutor* GetExecutor(FBCommandType type);

    // Register a custom executor (for plugins/integrations)
    void RegisterExecutor(FBCommandType type, ICommandExecutor* executor);
}
