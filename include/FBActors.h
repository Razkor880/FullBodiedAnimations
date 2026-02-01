#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <RE/Skyrim.h>
#include "FBStructs.h"

namespace FB
{


    //namespace Actors
    //{
        // Stubbed for now (keeps linkage clean while we move systems around).
        // When you implement paired interactions, this becomes the resolver/executor API.
        //void StartTimeline(
        //    RE::ActorHandle caster,
        //    RE::ActorHandle target,
        //    std::uint32_t casterFormID,
        //    bool logOps);

        //void CancelAndReset(
         //   RE::ActorHandle caster,
        //    std::uint32_t casterFormID,
        //    bool logOps,
        //    bool resetMorphCaster,
        //    bool resetMorphTarget);

        //void Update(float dtSeconds);

    //}
}
namespace FB::Actors {
    RE::Actor* ResolveActorForEvent(const FBEvent& e, ActorRole role);
}
