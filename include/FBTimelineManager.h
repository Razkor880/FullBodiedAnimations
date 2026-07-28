#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include "FBStructs.h"

namespace FB {
    // Optimized timeline management with O(1) actor lookups
    // Replaces the old vector-based timeline search

    class TimelineManager {
    public:
        TimelineManager() = default;
        ~TimelineManager() = default;

        // Add a new timeline
        void Add(const ActiveTimeline& timeline) {
            _timelinesByActor[timeline.event.actor.formID] = timeline;
            _allTimelines.push_back(timeline);
        }

        // Get timeline for a specific actor (O(1))
        [[nodiscard]] ActiveTimeline* GetTimelineForActor(std::uint32_t actorFormID) {
            auto it = _timelinesByActor.find(actorFormID);
            return it != _timelinesByActor.end() ? &it->second : nullptr;
        }

        [[nodiscard]] const ActiveTimeline* GetTimelineForActor(std::uint32_t actorFormID) const {
            auto it = _timelinesByActor.find(actorFormID);
            return it != _timelinesByActor.end() ? &it->second : nullptr;
        }

        // Get all active timelines (for iteration)
        [[nodiscard]] const std::vector<ActiveTimeline>& GetAllTimelines() const {
            return _allTimelines;
        }

        // Update a timeline's state
        void Update(std::uint32_t actorFormID, const ActiveTimeline& timeline) {
            _timelinesByActor[actorFormID] = timeline;
            // Also update in _allTimelines
            for (auto& tl : _allTimelines) {
                if (tl.event.actor.formID == actorFormID) {
                    tl = timeline;
                    break;
                }
            }
        }

        // Remove timeline for an actor (O(n) for cleanup in _allTimelines)
        bool Remove(std::uint32_t actorFormID) {
            auto removed = _timelinesByActor.erase(actorFormID) > 0;
            if (removed) {
                _allTimelines.erase(
                    std::remove_if(_allTimelines.begin(), _allTimelines.end(),
                                  [actorFormID](const ActiveTimeline& tl) {
                                      return tl.event.actor.formID == actorFormID;
                                  }),
                    _allTimelines.end());
            }
            return removed;
        }

        // Clear all timelines
        void Clear() {
            _timelinesByActor.clear();
            _allTimelines.clear();
        }

        // Check if timeline exists for actor
        [[nodiscard]] bool HasTimelineForActor(std::uint32_t actorFormID) const {
            return _timelinesByActor.find(actorFormID) != _timelinesByActor.end();
        }

        // Get count of active timelines
        [[nodiscard]] std::size_t GetCount() const {
            return _timelinesByActor.size();
        }

        // Get all actor IDs with active timelines
        [[nodiscard]] std::vector<std::uint32_t> GetActorIDs() const {
            std::vector<std::uint32_t> actors;
            for (const auto& [actorID, _] : _timelinesByActor) {
                actors.push_back(actorID);
            }
            return actors;
        }

    private:
        // Index: formID -> ActiveTimeline (for fast actor lookup)
        std::unordered_map<std::uint32_t, ActiveTimeline> _timelinesByActor;

        // Also maintain vector for iteration (mirrors the map)
        std::vector<ActiveTimeline> _allTimelines;
    };
}
