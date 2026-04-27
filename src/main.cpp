#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using rr::core::CommandLine;
    using rr::core::Config;
    using rr::core::Logger;

    Config cfg;
    const auto parse = CommandLine::parse(argc, argv, cfg);

    switch (parse.status) {
    case CommandLine::Status::Help:
        std::cout << CommandLine::usage();
        return 0;
    case CommandLine::Status::Version:
        std::cout << rr::core::kProjectName << ' ' << rr::core::kVersionString << '\n';
        return 0;
    case CommandLine::Status::Error:
        Logger::error(parse.message);
        std::cerr << CommandLine::usage();
        return 2;
    case CommandLine::Status::Ok:
        break;
    }

    Logger::info(std::string(rr::core::kProjectName) + " " + rr::core::kVersionString + " starting");

    if (cfg.show_device_info) {
        // The CUDA backend lands at M5; until then there is no device
        // enumeration to perform. Be honest about that rather than fake it.
        Logger::info("device-info: no GPU backend available yet (CUDA backend lands at M5)");
        return 0;
    }

    if (cfg.wants_render()) {
        // M2 scope: parse the request, do not render. Real rendering arrives
        // once the GPU layers (M5+) and path tracer (M14) are in.
        Logger::info("render command received");
        return 0;
    }

    Logger::info("No action requested. Use --help to see options.");
    return 0;
}
