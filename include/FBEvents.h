#include "FBStructs.h"

#include <mutex>
#include <vector>
#include <atomic>

namespace RE {
    struct BSAnimationGraphEvent;
}

class FBEvents
{
public:
    void Push(const FBEvent& event);

    std::vector<FBEvent> Drain();

    void Clear();

    std::size_t Size() const;

        // Event manager lifecycle hooks (called from FBPlugin message listener)
    void OnDataLoaded();
    void OnPostLoadOrNewGame();

    void HandleAnimEvent(const RE::BSAnimationGraphEvent& evn);

private:
    mutable std::mutex _mutex;

    std::vector<FBEvent> _queue;

        // Anim event plumbing (defined in FBEvents.cpp)
    void TryRegisterToPlayer();
    

    std::atomic_bool _registered{false};
    std::atomic_bool _sawAnyEvent{false};
    std::atomic_bool _logAllAnimTags{false};
};

