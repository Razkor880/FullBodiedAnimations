#include "FBUpdate.h"
#include "FBStructs.h"

#include <mutex>
#include <vector>

class FBEvents
{
public:
    void Push(const FBEvent& event);

    std::vector<FBEvent> Drain();

    void Clear();

    std::size_t Size() const;

private:
    mutable std::mutex _mutex;

    std::vector<FBEvent> _queue;
};