# CUDA-host Verification Report

Generator: `tools/verify_cuda_host.py` (CUDA-H.9)
Spec: `docs/CUDA_HOST_VERIFICATION_PLAN.md`
Tree state: `9218b18`

## Environment

- Binary: `build_off/bin/RelativityRender`
- `--optix`: off
- CUDA GPU: not detected (CUDA disabled or audit-host)
- `--device-info`: no critical errors

## Test results

| name | status | rc |
|------|--------|----|
| `device-info` | PASS | 0 |
| `render-gradient` | FAIL | 1 |
| `render-camera-rays` | FAIL | 1 |
| `render-sphere` | FAIL | 1 |
| `render-relativistic` | FAIL | 1 |
| `render-scene-spheres` | FAIL | 1 |
| `render-texture-sample-test` | FAIL | 1 |
| `render-textured-material` | FAIL | 1 |
| `render-aovs` | FAIL | 1 |
| `render-pathtrace` | FAIL | 1 |
| `render-optix-raygen` | SKIPPED | - |
| `render-optix-triangle` | SKIPPED | - |
| `render-optix-pathtrace` | SKIPPED | - |

## Summary

- pass    : 1
- fail    : 9
- skipped : 3
- overall : **REPAIR**

One or more commands failed. Inspect the runner's stderr output for the documented error per command and follow the REPAIR criteria in `docs/CUDA_HOST_VERIFICATION_PLAN.md` §5.
