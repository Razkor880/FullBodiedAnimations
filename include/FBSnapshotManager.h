#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include "FBStructs.h"

// Thread-safe configuration snapshot management
// Provides atomic snapshot swapping without readers needing locks

namespace FB::Config {
    class SnapshotManager {
    public:
        static SnapshotManager& GetInstance() {
            static SnapshotManager instance;
            return instance;
        }

        // Get current snapshot (safe to read from any thread)
        // Returned pointer is guaranteed to remain valid for the duration of use
        [[nodiscard]] std::shared_ptr<const Snapshot> GetSnapshot() const {
            return std::atomic_load(&_snapshot);
        }

        // Get generation number of current snapshot
        [[nodiscard]] Generation GetGeneration() const {
            auto snap = GetSnapshot();
            return snap ? snap->generation : 0;
        }

        // Atomically swap to a new snapshot
        // Returns true if swap succeeded (generation changed)
        bool SetSnapshot(std::shared_ptr<Snapshot> newSnapshot) {
            if (!newSnapshot) return false;

            std::lock_guard<std::mutex> lock(_swapMutex);
            auto old = std::atomic_load(&_snapshot);
            
            // Ensure generation is incremented
            if (old && newSnapshot->generation <= old->generation) {
                newSnapshot->generation = old->generation + 1;
            }

            std::atomic_store(&_snapshot, newSnapshot);
            return true;
        }

        // Initialize with first snapshot (typically called once at startup)
        void Initialize(std::shared_ptr<Snapshot> initial) {
            if (!initial) return;
            initial->generation = 1;
            std::atomic_store(&_snapshot, initial);
        }

    private:
        SnapshotManager() = default;
        ~SnapshotManager() = default;

        // Atomic shared_ptr to current snapshot
        // Uses std::atomic<std::shared_ptr<>> for thread-safe reads without locks
        std::atomic<std::shared_ptr<const Snapshot>> _snapshot;

        // Mutex for atomic swap (only needed during SetSnapshot)
        mutable std::mutex _swapMutex;
    };
}
