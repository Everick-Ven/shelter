#include "app/common/logging.h"
#include <fstream>
#include <mutex>
namespace shelter { namespace { std::mutex m; std::ofstream f; } void InitializeLogging(const char* p){std::lock_guard l(m);f.open(p,std::ios::app);} void Log(LogLevel,std::string_view s){std::lock_guard l(m);if(f){f<<s<<'\n';f.flush();}} }
