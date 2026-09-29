# OpenPBR BSDF

Vendored from [Adobe OpenPBR BSDF](https://github.com/adobe/openpbr-bsdf),
commit `c91aad1d1ce1693e803f039d7c92c2965c4eb013` (OpenPBR 1.1.1 implementation).
License: [Apache 2.0](LICENSE). The headers are unchanged except for one local
Slang compatibility patch in `impl/openpbr_thin_film_iridescence_utils.h`:
initialize dielectric `r23[0]` before looping over channels 1 and 2. This preserves
the Fresnel calculation while avoiding Slang's E41035 definite-assignment warning.

Vultra selects the upstream Slang backend in `builtin/shaders/lib/openpbr.slangh`.
The adapter maps the exposed opaque material parameters to `OpenPBR_ResolvedInputs`.
Direct illumination uses `openpbr_prepare` and `openpbr_eval`; the evaluator already
includes the light cosine. Vultra adds glTF emission separately, without coat
attenuation or suppression by perturbed shading normals.

The renderer uploads the upstream lookup arrays into one RGBA32F texture atlas
(`source/src/function/renderer/builtin/openpbr_luts.cpp`). The shader implements
the upstream texture-LUT callbacks with bilinear/trilinear interpolation in
`builtin/shaders/resources/openpbr_luts.slangh`. This avoids embedding large
constant arrays in SPIR-V and needs no 16-bit arithmetic or texture assets.
Only the data headers are used by the renderer's CPU code; the full C++ BSDF
backend is used by `tests/rendering.cpp` as a GPU comparison reference.
Vultra's split-sum environment lighting remains a separate approximation.

Shader hot reload watches `builtin/shaders`; rebuild and restart after updating this vendored
dependency. Keep local integration code in the adapter so upstream updates stay simple.
