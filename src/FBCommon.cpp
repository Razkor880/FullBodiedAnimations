#include "FBCommon.h"
#include <spdlog/spdlog.h>

namespace FB {
    LogLevel g_logLevel = LogLevel::Info;

    std::string ParseError::ToString() const {
        std::string result = file;
        if (lineNumber > 0) {
            result += ":" + std::to_string(lineNumber);
        }
        if (!section.empty()) {
            result += " [" + section + "]";
        }
        if (!key.empty()) {
            result += " key='" + key + "'";
        }
        result += " : " + message;
        if (isCritical) {
            result += " (CRITICAL)";
        }
        return result;
    }

    void ParseErrorCollector::Add(const ParseError& error) {
        _errors.push_back(error);
        if (error.isCritical) {
            _hasCritical = true;
        }
    }

    void ParseErrorCollector::AddError(std::string_view file, int line, 
                                       std::string_view section, std::string_view key, 
                                       std::string_view message, bool critical) {
        ParseError err;
        err.file = std::string(file);
        err.lineNumber = line;
        err.section = std::string(section);
        err.key = std::string(key);
        err.message = std::string(message);
        err.isCritical = critical;
        Add(err);
    }

    void ParseErrorCollector::ReportSummary() const {
        if (_errors.empty()) {
            return;
        }

        spdlog::warn("[FB] Parse Summary: {} errors ({} critical)", _errors.size(), 
                     std::count_if(_errors.begin(), _errors.end(), 
                                   [](const ParseError& e) { return e.isCritical; }));

        for (const auto& err : _errors) {
            if (err.isCritical) {
                spdlog::error("  [CRITICAL] {}", err.ToString());
            } else {
                spdlog::warn("  [WARN] {}", err.ToString());
            }
        }
    }
}
