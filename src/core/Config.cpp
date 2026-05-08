#include "core/Config.h"

namespace rr::core {

std::string Config::validate() const {
    if (width  <= 0) return "width must be positive";
    if (height <= 0) return "height must be positive";
    return {};
}

}
