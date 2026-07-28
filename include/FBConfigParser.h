#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include "FBStructs.h"
#include "FBCommon.h"

namespace FB::Config {
    // Modularized INI parsing components

    // ===== INI Section/Key Parsing =====
    struct IniSection {
        std::string name;
        std::unordered_map<std::string, std::string> values;  // key -> value
    };

    class IniDocument {
    public:
        bool LoadFromFile(std::string_view filePath, ParseErrorCollector& errors);
        const IniSection* GetSection(std::string_view name) const;
        std::string GetValue(std::string_view section, std::string_view key, 
                             std::string_view defaultValue = "") const;
        bool GetBool(std::string_view section, std::string_view key, bool defaultValue = false) const;
        float GetFloat(std::string_view section, std::string_view key, float defaultValue = 0.0f) const;

    private:
        std::unordered_map<std::string, IniSection> _sections;
        friend class SnapshotBuilder;
    };

    // ===== Snapshot Construction =====
    class SnapshotBuilder {
    public:
        static std::shared_ptr<Snapshot> BuildFromGeneralIni(
            const IniDocument& generalIni,
            ParseErrorCollector& errors);
        
    private:
        static bool ParsePerAnimIni(
            std::string_view alias,
            std::string_view clip,
            const std::string& variantsDir,
            TimedCommandList& outCommands,
            ParseErrorCollector& errors);
    };

    // ===== Configuration Snapshot Loader =====
    class SnapshotLoader {
    public:
        // Load initial configuration from INI files
        // Returns null if critical errors prevent loading
        static std::shared_ptr<Snapshot> LoadInitial(ParseErrorCollector& errors);
        
        // Reload configuration (atomic swap)
        static bool Reload(std::shared_ptr<Snapshot>& outSnapshot, ParseErrorCollector& errors);
    };
}
