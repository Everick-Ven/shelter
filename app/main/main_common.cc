#include "app/common/logging.h"
int main(){shelter::InitializeLogging("shelter.log");shelter::Log(shelter::LogLevel::Info,"SHELTER foundation");return 0;}
