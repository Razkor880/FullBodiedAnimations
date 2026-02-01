#pragma once
#include <string_view>

namespace FB::Maps {
    std::string_view ResolveNode(std::string_view key);

    // Future (stub): RaceMenu slider / morph key mapping
    std::string_view ResolveMorph(std::string_view key);
}
