#include "FBMaps.h"

#include <unordered_map>

namespace {
    // string literals only => stable string_view targets
    const std::unordered_map<std::string_view, std::string_view> kNodeMap{
        {"Head", "NPC Head [Head]"},     {"Pelvis", "NPC Pelvis [Pelv]"}, {"Spine", "NPC Spine [Spn0]"},
        {"Spine1", "NPC Spine1 [Spn1]"}, {"Spine2", "NPC Spine2 [Spn2]"},
    };
}

namespace FB::Maps {
    std::string_view ResolveNode(std::string_view key) {
        if (auto it = kNodeMap.find(key); it != kNodeMap.end()) {
            return it->second;
        }
        return key;  // pass-through
    }

    std::string_view ResolveMorph(std::string_view key) {
        return key;  // stub pass-through for now
    }
}
