#pragma once

#include <string>

namespace rr::core {

struct Config;

// Command-line argument parser for the RelativityRender executable.
//
// Stateless. Populates a `Config` from `argc`/`argv` and reports back what
// the caller should do next via `Status`. The parser does not print
// anything; `main` decides how `Help`, `Version`, and `Error` are surfaced.
class CommandLine {
public:
    enum class Status {
        Ok,       // parse succeeded; run normally
        Help,     // `--help` (or `-h`) was seen; print usage and exit 0
        Version,  // `--version` (or `-v`) was seen; print version and exit 0
        Error     // parse failure; `message` describes it
    };

    struct ParseResult {
        Status      status = Status::Ok;
        std::string message;  // populated only when `status == Error`
    };

    // Parse argv into out. Returns the status the caller should act on.
    static ParseResult parse(int argc, char** argv, Config& out);

    // Multi-line usage string suitable for `--help` output.
    static std::string usage();
};

}
