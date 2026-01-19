#include "FBEvents.h"

void FBEvents::Push(const FBEvent& event) 
{
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.push_back(event);
}

std::vector<FBEvent> FBEvents::Drain()
{
    std::vector<FBEvent> out;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        out.swap(_queue);
    }

    return out;
}

void FBEvents::Clear()
{ 
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.clear();
}

std::size_t FBEvents::Size() const 
{ 
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.size();
}