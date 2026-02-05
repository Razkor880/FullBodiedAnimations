#pragma once
#include <string_view>
#include <optional>

namespace FB::Maps {
    std::string_view ResolveNode(std::string_view key);

    // Morph mapping: INI-friendly key -> actual RaceMenu morph name (or pass-through)
    std::string_view ResolveMorph(std::string_view key);

    // Expression support (name -> index/id)
    std::optional<std::int32_t> TryGetPhonemeIndex(std::string_view name);
    std::optional<std::int32_t> TryGetMoodId(std::string_view name);
    std::optional<std::int32_t> TryGetModifierIndex(std::string_view name);
}