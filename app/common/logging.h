#pragma once
#include <string_view>
namespace shelter { enum class LogLevel { Debug, Info, Warning, Error }; void InitializeLogging(const char*); void Log(LogLevel,std::string_view); }
