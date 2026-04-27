#include "core/Logger.h"
#include "core/Version.h"

#include <string>

int main(int /*argc*/, char** /*argv*/) {
    using rr::core::Logger;

    Logger::info(std::string(rr::core::kProjectName) + " " + rr::core::kVersionString + " starting");
    Logger::info("GPU renderer platform with relativistic perception model");
    Logger::info("Core application foundation online");
    return 0;
}
