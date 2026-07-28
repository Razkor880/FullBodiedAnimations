#pragma once

#include <unordered_map>
#include <string>
#include <cstdint>
#include "FBUpdate.h"

namespace FB {
    // Optimized tween management with O(1) actor lookups
    // Replaces the old string-key-prefix based tween map

    class TweenManager {
    public:
        TweenManager() = default;
        ~TweenManager() = default;

        // Add or update a tween for an actor
        void AddTween(std::uint32_t actorFormID, const std::string& key, const FBUpdate::ActiveTween& tween) {
            _tweensByActor[actorFormID][key] = tween;
        }

        // Get all tweens for an actor
        [[nodiscard]] bool GetTweensForActor(std::uint32_t actorFormID, 
                                             std::vector<FBUpdate::ActiveTween>& outTweens) const {
            auto it = _tweensByActor.find(actorFormID);
            if (it == _tweensByActor.end()) return false;

            outTweens.clear();
            for (const auto& [_, tween] : it->second) {
                outTweens.push_back(tween);
            }
            return true;
        }

        // Get a specific tween by actor and key
        [[nodiscard]] const FBUpdate::ActiveTween* GetTween(std::uint32_t actorFormID, 
                                                             const std::string& key) const {
            auto actorIt = _tweensByActor.find(actorFormID);
            if (actorIt == _tweensByActor.end()) return nullptr;

            auto tweenIt = actorIt->second.find(key);
            return tweenIt != actorIt->second.end() ? &tweenIt->second : nullptr;
        }

        // Update a specific tween
        void UpdateTween(std::uint32_t actorFormID, const std::string& key, 
                        const FBUpdate::ActiveTween& tween) {
            _tweensByActor[actorFormID][key] = tween;
        }

        // Remove a specific tween
        bool RemoveTween(std::uint32_t actorFormID, const std::string& key) {
            auto actorIt = _tweensByActor.find(actorFormID);
            if (actorIt == _tweensByActor.end()) return false;

            return actorIt->second.erase(key) > 0;
        }

        // Remove all tweens for an actor
        bool RemoveAllTweensForActor(std::uint32_t actorFormID) {
            return _tweensByActor.erase(actorFormID) > 0;
        }

        // Get all actor IDs with active tweens
        [[nodiscard]] std::vector<std::uint32_t> GetActorsWithTweens() const {
            std::vector<std::uint32_t> actors;
            for (const auto& [actorID, _] : _tweensByActor) {
                actors.push_back(actorID);
            }
            return actors;
        }

        // Clear all tweens
        void Clear() {
            _tweensByActor.clear();
        }

        // Check if there are any active tweens
        [[nodiscard]] bool HasAnyTweens() const {
            return !_tweensByActor.empty();
        }

        // Get total tween count
        [[nodiscard]] std::size_t GetTweenCount() const {
            std::size_t count = 0;
            for (const auto& [_, tweens] : _tweensByActor) {
                count += tweens.size();
            }
            return count;
        }

    private:
        // Structure: formID -> (tweenKey -> ActiveTween)
        std::unordered_map<std::uint32_t, std::unordered_map<std::string, FBUpdate::ActiveTween>> _tweensByActor;
    };
}
