// RelativityRender entry point.
//
// Stage 1 (Core app) scope: print the version and a startup message.
// No GPU, no rendering, no scene system, no server. Subsequent stages
// (math library, image / framebuffer, GPU device layer, ...) layer
// real capability on top of this skeleton in the order documented in
// `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`.

#include "core/Logger.h"
#include "core/Version.h"

#include <string>

int main() {
    rr::core::Logger::info(std::string(rr::core::kProjectName) + " "
                         + rr::core::kVersionString + " starting up.");
    rr::core::Logger::info("Stage 1: core application skeleton. "
                           "No GPU, no renderer, no scene yet.");
    return 0;
}
