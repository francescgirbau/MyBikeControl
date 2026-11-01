#include "mybc/log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>

namespace mybc {
namespace {

std::mutex g_logMutex;
LogLevel g_minLevel = LogLevel::Info;

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string timestamp() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto millis = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t seconds = system_clock::to_time_t(now);

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &seconds);
#else
    localtime_r(&seconds, &tm);
#endif

    char timebuf[16];
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm);

    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03lld", timebuf, static_cast<long long>(millis.count()));
    return out;
}

}  // namespace

void setLogLevel(LogLevel level) { g_minLevel = level; }

void logMessage(LogLevel level, const std::string& message) {
    if (static_cast<int>(level) < static_cast<int>(g_minLevel)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cerr << timestamp() << " [" << levelName(level) << "] " << message << std::endl;
}

void logDebug(const std::string& message) { logMessage(LogLevel::Debug, message); }
void logInfo(const std::string& message) { logMessage(LogLevel::Info, message); }
void logWarn(const std::string& message) { logMessage(LogLevel::Warn, message); }
void logError(const std::string& message) { logMessage(LogLevel::Error, message); }

}  // namespace mybc
