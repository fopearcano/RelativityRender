#include "optix/OptixBackend.h"

namespace rr::optix {

bool OptixBackend::isCompiled() noexcept {
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    return true;
#else
    return false;
#endif
}

}  // namespace rr::optix
