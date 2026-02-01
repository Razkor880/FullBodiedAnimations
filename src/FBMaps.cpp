#include "FBMaps.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace {
    // string literals only => stable string_view targets
    static const std::unordered_map<std::string_view, std::string_view> kNodeMap{
        // --- ROOT / CORE ---
        {"NPC", "NPC"},
        {"Root", "NPC Root [Root]"},
        {"COM", "NPC COM [COM ]"},  // note: COM has a trailing space inside brackets in some lists

        // --- TORSO / HEAD ---
        {"Pelvis", "NPC Pelvis [Pelv]"},
        {"Spine", "NPC Spine [Spn0]"},
        {"Spine0", "NPC Spine [Spn0]"},
        {"Spine1", "NPC Spine1 [Spn1]"},
        {"Spine2", "NPC Spine2 [Spn2]"},
        {"Neck", "NPC Neck [Neck]"},
        {"Head", "NPC Head [Head]"},

        // --- LEFT ARM ---
        {"LClavicle", "NPC L Clavicle [LClv]"},
        {"LeftClavicle", "NPC L Clavicle [LClv]"},
        {"LUpperArm", "NPC L UpperArm [LUar]"},
        {"LeftUpperArm", "NPC L UpperArm [LUar]"},
        {"LForearm", "NPC L Forearm [LLar]"},
        {"LeftForearm", "NPC L Forearm [LLar]"},
        {"LHand", "NPC L Hand [LHnd]"},
        {"LeftHand", "NPC L Hand [LHnd]"},

        // --- RIGHT ARM ---
        {"RClavicle", "NPC R Clavicle [RClv]"},
        {"RightClavicle", "NPC R Clavicle [RClv]"},
        {"RUpperArm", "NPC R UpperArm [RUar]"},
        {"RightUpperArm", "NPC R UpperArm [RUar]"},
        {"RForearm", "NPC R Forearm [RLar]"},
        {"RightForearm", "NPC R Forearm [RLar]"},
        {"RHand", "NPC R Hand [RHnd]"},
        {"RightHand", "NPC R Hand [RHnd]"},

        // --- LEFT LEG ---
        {"LThigh", "NPC L Thigh [LThg]"},
        {"LeftThigh", "NPC L Thigh [LThg]"},
        {"LCalf", "NPC L Calf [LClf]"},
        {"LeftCalf", "NPC L Calf [LClf]"},
        {"LFoot", "NPC L Foot [LLft ]"},
        {"LeftFoot", "NPC L Foot [LLft ]"},
        {"LToe", "NPC L Toe0 [LToe]"},
        {"LeftToe", "NPC L Toe0 [LToe]"},

        // --- RIGHT LEG ---
        {"RThigh", "NPC R Thigh [RThg]"},
        {"RightThigh", "NPC R Thigh [RThg]"},
        {"RCalf", "NPC R Calf [RClf]"},
        {"RightCalf", "NPC R Calf [RClf]"},
        {"RFoot", "NPC R Foot [Rft ]"},
        {"RightFoot", "NPC R Foot [Rft ]"},
        {"RToe", "NPC R Toe0 [RToe]"},
        {"RightToe", "NPC R Toe0 [RToe]"},

        // --- QUALITY-OF-LIFE ALIASES (your older short tokens, just in case) ---
        {"Pelv", "NPC Pelvis [Pelv]"},
        {"Spn0", "NPC Spine [Spn0]"},
        {"Spn1", "NPC Spine1 [Spn1]"},
        {"Spn2", "NPC Spine2 [Spn2]"},
        {"Neck", "NPC Neck [Neck]"},
        {"Head", "NPC Head [Head]"},
        {"LThg", "NPC L Thigh [LThg]"},
        {"RThg", "NPC R Thigh [RThg]"},
        {"LClf", "NPC L Calf [LClf]"},
        {"RClf", "NPC R Calf [RClf]"},
        {"LLft", "NPC L Foot [LLft ]"},
        {"Rft", "NPC R Foot [Rft ]"},
        {"LHnd", "NPC L Hand [LHnd]"},
        {"RHnd", "NPC R Hand [RHnd]"},
        {"LUar", "NPC L UpperArm [LUar]"},
        {"RUar", "NPC R UpperArm [RUar]"},
        {"LLar", "NPC L Forearm [LLar]"},
        {"RLar", "NPC R Forearm [RLar]"},
        {"LClv", "NPC L Clavicle [LClv]"},
        {"RClv", "NPC R Clavicle [RClv]"},
    };

    // log unknown keys once to avoid spam
    static std::unordered_set<std::string> g_unknownNodeKeys;
}

namespace FB::Maps {
    std::string_view ResolveNode(std::string_view key) {
        if (key.empty()) {
            return key;
        }

        if (auto it = kNodeMap.find(key); it != kNodeMap.end()) {
            return it->second;
        }

        // pass-through, but log once
        if (g_unknownNodeKeys.insert(std::string(key)).second) {
            spdlog::debug("[FB] Maps: ResolveNode pass-through key='{}'", key);
        }

        return key;
    }

    std::string_view ResolveMorph(std::string_view key) {
        return key;  // stub pass-through for now
    }
}
