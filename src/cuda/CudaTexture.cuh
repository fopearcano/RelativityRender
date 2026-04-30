#pragma once

// CUDA-side variant of the texture data model (Stage 13A; master
// order #18).
//
// Currently a thin re-export of the host headers so kernels can
// `#include "cuda/CudaTexture.cuh"` to signal intent. There is no
// `RR_HD inline` `sample(uv)` helper yet, no `cudaTextureObject_t`
// lifecycle, no mipmap construction, no UV transform. Stage 13A
// is data model only - the device-side sampler (UV-keyed lookup
// helpers) and the GPU upload path (host
// `ImageTexture::pixels` -> CUDA array -> bindless
// `cudaTextureObject_t` descriptor) join in a later sub-stage.
//
// `Texture` and `ImageTexture` are both host-side authoring types
// today; their POD-friendly fields make a device-side descriptor
// (texture id + kind + constant-color OR image-handle) a small
// additional struct when the sampler lands. Until then, kernels
// that want to reference the upstream types include this header,
// mirroring `CudaMaterial.cuh` / `CudaLight.cuh`.

#include "texture/ImageTexture.h"
#include "texture/Texture.h"
