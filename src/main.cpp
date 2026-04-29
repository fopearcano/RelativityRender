// RelativityRender entry point.
//
// Stage 1 (Core app) scope: parse command-line flags and dispatch a
// stage-appropriate response. No GPU, no rendering, no scene system,
// no server. Subsequent stages (math library, image / framebuffer,
// GPU device layer, ...) layer real capability on top of this
// skeleton in the order documented in
// `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`.

#include "core/CommandLine.h"
#include "core/Logger.h"
#include "core/Version.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using rr::core::CommandLine;
    using rr::core::Logger;

    const auto result = CommandLine::parse(argc, argv);

    switch (result.action) {
        case CommandLine::Action::Help:
            std::cout << CommandLine::usage(argv[0]);
            return 0;

        case CommandLine::Action::Version:
            std::cout << CommandLine::version_string() << '\n';
            return 0;

        case CommandLine::Action::DeviceInfo:
            Logger::info("GPU device info not implemented yet");
            return 0;

        case CommandLine::Action::Render:
            Logger::info("render command received");
            return 0;

        case CommandLine::Action::Error:
            Logger::error(result.error_message);
            std::cerr << CommandLine::usage(argv[0]);
            return 2;

        case CommandLine::Action::Default:
            Logger::info(std::string(rr::core::kProjectName) + " "
                       + rr::core::kVersionString + " starting up.");
            Logger::info("Stage 1: core application skeleton. "
                         "No GPU, no renderer, no scene yet.");
            return 0;
    }
    return 0;
}
