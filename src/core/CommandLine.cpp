#include "core/CommandLine.h"

#include "core/Config.h"

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

namespace rr::core {

namespace {

// Try to read the value that follows a flag at position `i`. Returns
// nullptr if there is no next argument.
const char* next_value(int argc, char** argv, int i) {
    if (i + 1 >= argc) return nullptr;
    return argv[i + 1];
}

bool parse_positive_int(std::string_view text, int& out) {
    int value = 0;
    const auto* begin = text.data();
    const auto* end   = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value <= 0) {
        return false;
    }
    out = value;
    return true;
}

}

CommandLine::ParseResult CommandLine::parse(int argc, char** argv, Config& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            return {Status::Help, {}};
        }
        if (arg == "--version" || arg == "-v") {
            return {Status::Version, {}};
        }
        if (arg == "--device-info") {
            out.show_device_info = true;
            continue;
        }
        if (arg == "--serve") {
            out.serve = true;
            continue;
        }
        if (arg == "--render") {
            const char* v = next_value(argc, argv, i);
            if (!v) return {Status::Error, "--render requires a scene file path"};
            out.render_scene_path = v;
            ++i;
            continue;
        }
        if (arg == "--output") {
            const char* v = next_value(argc, argv, i);
            if (!v) return {Status::Error, "--output requires an image path"};
            out.output_image_path = v;
            ++i;
            continue;
        }
        if (arg == "--width") {
            const char* v = next_value(argc, argv, i);
            if (!v) return {Status::Error, "--width requires a positive integer"};
            int w = 0;
            if (!parse_positive_int(v, w)) {
                return {Status::Error, "--width must be a positive integer"};
            }
            out.width = w;
            ++i;
            continue;
        }
        if (arg == "--height") {
            const char* v = next_value(argc, argv, i);
            if (!v) return {Status::Error, "--height requires a positive integer"};
            int h = 0;
            if (!parse_positive_int(v, h)) {
                return {Status::Error, "--height must be a positive integer"};
            }
            out.height = h;
            ++i;
            continue;
        }

        return {Status::Error, "unknown argument: " + std::string(arg)};
    }

    return {Status::Ok, {}};
}

std::string CommandLine::usage() {
    return
        "Usage: RelativityRender [options]\n"
        "\n"
        "Options:\n"
        "  -h, --help                Show this message and exit.\n"
        "  -v, --version             Print version and exit.\n"
        "      --device-info         Print available GPU devices and exit.\n"
        "      --serve               Run as renderer server on 127.0.0.1:7777.\n"
        "      --render <scene>      Render the given scene file.\n"
        "      --output <path>       Image output path (used with --render).\n"
        "      --width  <pixels>     Image width  (default: 1280).\n"
        "      --height <pixels>     Image height (default: 720).\n";
}

}
