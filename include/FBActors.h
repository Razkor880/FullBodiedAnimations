#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <RE/Skyrim.h>

namespace FB
{
    // Who a command targets in a paired interaction
    enum class TargetKind : std::uint8_t
    {
        kCaster,
        kTarget
    };

    // What kind of command it is (Phase 1/2 support)
    enum class CommandKind : std::uint8_t
    {
        kScale,
        kMorph,
        kHide
    };

    // Reserved for future tween/easing expansion (keep compiling now)
    enum class TweenCurve : std::uint8_t
    {
        kLinear
    };

    enum class HideMode : std::uint8_t
    {
        kAll,
        kSlot
    };

    // The parsed "timed command" payload used by FBConfig + the executor.
    struct TimedCommand
    {
        CommandKind kind{ CommandKind::kScale };
        TargetKind  target{ TargetKind::kCaster };
        float       timeSeconds{ 0.0f };

        // Scale payload (valid when kind==kScale)
        std::string_view nodeKey{};
        float            scale{ 1.0f };

        // Morph payload (valid when kind==kMorph)
        std::string morphName{};
        float       delta{ 0.0f };

        // Tween payload (optional; used only when kind==kMorph)
        float tweenSeconds{ 0.0f };
        TweenCurve tweenCurve{ TweenCurve::kLinear };

        // Hide payload (valid when kind==kHide)
        HideMode      hideMode{ HideMode::kAll };
        std::uint16_t hideSlot{ 0 };  // valid when hideMode==kSlot
        bool          hide{ false };
    };

    namespace Actors
    {
        // Stubbed for now (keeps linkage clean while we move systems around).
        // When you implement paired interactions, this becomes the resolver/executor API.
        void StartTimeline(
            RE::ActorHandle caster,
            RE::ActorHandle target,
            std::uint32_t casterFormID,
            std::vector<FB::TimedCommand> commands,
            bool logOps);

        void CancelAndReset(
            RE::ActorHandle caster,
            std::uint32_t casterFormID,
            bool logOps,
            bool resetMorphCaster,
            bool resetMorphTarget);

        void Update(float dtSeconds);
    }
}
