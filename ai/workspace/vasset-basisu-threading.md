# vasset BasisU Threading

## Context

`VTextureImporter::ImportOptions::basisUThreadCount` used `0` as the public
default, but the importer forwarded that value directly into
`ktxBasisParams::threadCount`. In practice this made the default encode path
ambiguous and could leave BasisU under-threaded during slow texture imports.

## Change

- Resolve `basisUThreadCount == 0` to `std::thread::hardware_concurrency()`,
  falling back to `1` if the platform cannot report a count.
- Keep non-zero explicit values unchanged.
- Include the resolved BasisU thread count in the existing import diagnostic.

## Follow-up Default Tuning

The previous default was ETC1S with `qualityLevel = 255` and
`compressionLevel = 2`, which is a poor editor default: ETC1S can visibly block
large albedo textures, and the maximum quality setting increases import cost.

The importer default now favors fast, low-artifact imports:

- `uastc = true`
- `compressionLevel = 0`
- `qualityLevel = 128` for callers that explicitly switch back to ETC1S

This trades larger KTX2 payloads for faster import and more stable visual
quality. Slow, smaller package-quality compression should be exposed as an
explicit profile later instead of being the default import path.

Current KTX xmake config requests `decoder`, `vulkan`, static linkage, and
desktop-only OpenCL support. The local package recipe exposes `opencl`, not
OpenCV. `ktxBasisParams` does not expose a per-compress OpenCL toggle, so the
runtime choice remains inside KTX/BasisU once the library is built with
`BASISU_SUPPORT_OPENCL`. Because KTX is linked statically, `opencl` is also a
public `vasset` package dependency so final applications resolve the OpenCL C
API symbols pulled in by `basisu_opencl.cpp`.

## Verification

- `xmake build -y vultra-app` passed.
- After enabling KTX OpenCL, the first build installed `opencl v2023.04.17`
  through xmake and `xmake build -y vultra-app` passed.
