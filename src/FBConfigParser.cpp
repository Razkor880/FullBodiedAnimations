#include "FBConfigParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstring>

namespace FB::Config {
    namespace {
        // Utility functions
        static inline void FBTrimInPlace(std::string& s) {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
            s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        }

        static inline void StripInlineComment(std::string& s) {
            auto posHash = s.find('#');
            auto posSemi = s.find(';');
            auto pos = std::min(posHash == std::string::npos ? s.size() : posHash,
                                posSemi == std::string::npos ? s.size() : posSemi);
            if (pos != std::string::npos && pos < s.size()) s.erase(pos);
        }

        static inline bool IEquals(const std::string& a, const char* b) {
            if (a.size() != std::strlen(b)) return false;
            for (size_t i = 0; i < a.size(); i++) {
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
            }
            return true;
        }

        static inline std::string ToLowerCopy(std::string s) {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
    }

    // ===== IniDocument Implementation =====
    bool IniDocument::LoadFromFile(std::string_view filePath, ParseErrorCollector& errors) {
        std::ifstream file(std::string(filePath));
        if (!file.good()) {
            errors.AddError(std::string(filePath), 0, "", "", "File not found or cannot be opened", true);
            return false;
        }

        std::string currentSection;
        std::string line;
        int lineNumber = 0;

        while (std::getline(file, line)) {
            lineNumber++;
            StripInlineComment(line);
            FBTrimInPlace(line);
            if (line.empty()) continue;

            // Section header
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                FBTrimInPlace(currentSection);
                if (_sections.find(currentSection) == _sections.end()) {
                    _sections[currentSection] = IniSection{currentSection, {}};
                }
                continue;
            }

            if (currentSection.empty()) {
                errors.AddError(std::string(filePath), lineNumber, "", "", 
                               "Key-value pair found outside any section", false);
                continue;
            }

            // Key=Value pair
            auto eq = line.find('=');
            if (eq == std::string::npos) {
                errors.AddError(std::string(filePath), lineNumber, currentSection, "", 
                               "Malformed key=value pair (missing '=')", false);
                continue;
            }

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            FBTrimInPlace(key);
            FBTrimInPlace(val);

            if (key.empty()) {
                errors.AddError(std::string(filePath), lineNumber, currentSection, "", 
                               "Empty key", false);
                continue;
            }

            _sections[currentSection].values[key] = val;
        }

        return true;
    }

    const IniSection* IniDocument::GetSection(std::string_view name) const {
        auto it = _sections.find(std::string(name));
        return it != _sections.end() ? &it->second : nullptr;
    }

    std::string IniDocument::GetValue(std::string_view section, std::string_view key, 
                                      std::string_view defaultValue) const {
        const auto* sec = GetSection(section);
        if (!sec) return std::string(defaultValue);

        auto it = sec->values.find(std::string(key));
        return it != sec->values.end() ? it->second : std::string(defaultValue);
    }

    bool IniDocument::GetBool(std::string_view section, std::string_view key, bool defaultValue) const {
        std::string val = GetValue(section, key, "");
        if (val.empty()) return defaultValue;
        val = ToLowerCopy(val);
        return val == "true" || val == "1" || val == "yes" || val == "on";
    }

    float IniDocument::GetFloat(std::string_view section, std::string_view key, float defaultValue) const {
        std::string val = GetValue(section, key, "");
        if (val.empty()) return defaultValue;
        try {
            return std::stof(val);
        } catch (...) {
            return defaultValue;
        }
    }

    // ===== SnapshotBuilder Implementation (stub) =====
    std::shared_ptr<Snapshot> SnapshotBuilder::BuildFromGeneralIni(
        const IniDocument& generalIni,
        ParseErrorCollector& errors) {
        auto snapshot = std::make_shared<Snapshot>();
        snapshot->generation = 1;

        // TODO: Full implementation with per-anim INI parsing
        // For now, this is a stub that preserves existing behavior
        
        return snapshot;
    }

    // ===== SnapshotLoader Implementation (stub) =====
    std::shared_ptr<Snapshot> SnapshotLoader::LoadInitial(ParseErrorCollector& errors) {
        // TODO: Full implementation
        auto snapshot = std::make_shared<Snapshot>();
        snapshot->generation = 1;
        return snapshot;
    }

    bool SnapshotLoader::Reload(std::shared_ptr<Snapshot>& outSnapshot, ParseErrorCollector& errors) {
        // TODO: Full implementation
        auto newSnapshot = std::make_shared<Snapshot>();
        newSnapshot->generation = outSnapshot->generation + 1;
        outSnapshot = newSnapshot;
        return true;
    }
}
