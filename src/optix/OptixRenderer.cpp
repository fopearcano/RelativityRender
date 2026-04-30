#include "optix/OptixRenderer.h"

namespace rr::optix {

OptixRenderer::Result OptixRenderer::render() noexcept {
    Result r;
    r.ok = false;
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    r.message =
        "OptiX backend not yet implemented (Stage 12B.2 placeholder; "
        "RELATIVITYRENDER_ENABLE_OPTIX is defined but no OptiX "
        "functionality is wired yet - sources/headers/SDK arrive in "
        "subsequent 12B sub-stages).";
#else
    r.message =
        "OptiX backend not compiled in (Stage 12B.2 placeholder; "
        "rebuild with -DRELATIVITYRENDER_ENABLE_OPTIX=ON to opt into "
        "the future OptiX path - though even ON, no OptiX functionality "
        "is wired yet).";
#endif
    return r;
}

}  // namespace rr::optix
