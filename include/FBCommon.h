#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <spdlog/spdlog.h>

// Common utilities and infrastructure shared across FB modules

namespace FB {
    // Log level control (configurable via INI [Debug] section)
    enum class LogLevel {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical
    };

    // Global log level (default: Info)
    extern LogLevel g_logLevel;

    // Convenience logging that respects global log level
    inline void LogTrace(std::string_view msg) {
        if (g_logLevel <= LogLevel::Trace) spdlog::trace("[FB] {}", msg);
    }
    inline void LogDebug(std::string_view msg) {
        if (g_logLevel <= LogLevel::Debug) spdlog::debug("[FB] {}", msg);
    }
    inline void LogInfo(std::string_view msg) {
        if (g_logLevel <= LogLevel::Info) spdlog::info("[FB] {}", msg);
    }
    inline void LogWarn(std::string_view msg) {
        if (g_logLevel <= LogLevel::Warn) spdlog::warn("[FB] {}", msg);
    }
    inline void LogError(std::string_view msg) {
        if (g_logLevel <= LogLevel::Error) spdlog::error("[FB] {}", msg);
    }

    // Error collection for INI parsing
    struct ParseError {
        std::string file;
        int lineNumber = 0;
        std::string section;
        std::string key;
        std::string message;
        bool isCritical = false;  // critical = abort parsing; non-critical = warn and continue

        [[nodiscard]] std::string ToString() const;
    };

    class ParseErrorCollector {
    public:
        void Add(const ParseError& error);
        void AddError(std::string_view file, int line, std::string_view section, 
                      std::string_view key, std::string_view message, bool critical = false);
        [[nodiscard]] bool HasCriticalErrors() const { return _hasCritical; }
        [[nodiscard]] bool HasAnyErrors() const { return !_errors.empty(); }
        [[nodiscard]] const std::vector<ParseError>& GetErrors() const { return _errors; }
        void ReportSummary() const;

    private:
        std::vector<ParseError> _errors;
        bool _hasCritical = false;
    };
}
