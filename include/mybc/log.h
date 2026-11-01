#pragma once

#include <string>

namespace mybc {

enum class LogLevel { Debug, Info, Warn, Error };

void setLogLevel(LogLevel level);
void logMessage(LogLevel level, const std::string& message);

void logDebug(const std::string& message);
void logInfo(const std::string& message);
void logWarn(const std::string& message);
void logError(const std::string& message);

}  // namespace mybc
