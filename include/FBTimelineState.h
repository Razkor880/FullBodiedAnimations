#pragma once

#include <cstdint>

// Formal timeline state machine to replace boolean flags
// States: Idle → Running → Complete → Resetting → Done
// Transitions are documented and explicit.

enum class TimelineState : std::uint8_t {
    Idle,        // Not yet started
    Running,     // Executing commands
    Complete,    // All commands executed, waiting for pair-end or timeout
    Resetting,   // Reset phase in progress (reverting transforms/morphs)
    Done         // Timeline can be removed
};

namespace FB {
    namespace Timeline {
        // Helper to convert state to string for logging
        inline const char* StateToString(TimelineState state) {
            switch (state) {
                case TimelineState::Idle:      return "Idle";
                case TimelineState::Running:   return "Running";
                case TimelineState::Complete:  return "Complete";
                case TimelineState::Resetting: return "Resetting";
                case TimelineState::Done:      return "Done";
                default:                       return "Unknown";
            }
        }

        // State transition rules
        inline bool CanTransition(TimelineState from, TimelineState to) {
            switch (from) {
                case TimelineState::Idle:
                    return to == TimelineState::Running || to == TimelineState::Done;
                case TimelineState::Running:
                    return to == TimelineState::Complete || to == TimelineState::Resetting;
                case TimelineState::Complete:
                    return to == TimelineState::Resetting || to == TimelineState::Done;
                case TimelineState::Resetting:
                    return to == TimelineState::Done;
                case TimelineState::Done:
                    return false;  // Terminal state
                default:
                    return false;
            }
        }
    }
}
