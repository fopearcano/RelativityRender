#include "optix/OptixBackend.h"

namespace rr::optix {

bool OptixBackend::isCompiled() noexcept {
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    return true;
#else
    return false;
#endif
}

bool OptixBackend::isSdkFound() noexcept {
#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    return true;
#else
    return false;
#endif
}

}  // namespace rr::optix
