# RT Lighting Raster Parity

## Summary

- Aligned ray tracing material base color with raster GBuffer by converting sampled base color from sRGB to linear before lighting.
- Replaced the old RT diffuse-only direct light with the same direct PBR BRDF shape used by raster deferred lighting.
- Matched RT point light falloff with raster point light attenuation.
- Kept non-shadow-casting lights visible in RT lighting while using `castsShadow` only to decide whether RT shadow rays are traced.
- Replaced fixed RT ambient `baseColor * 0.05` with scene/default ambient color, intensity, and material AO.

## Notes

- RT still does not implement raster IBL, reflection probes, spot lights, or area lights.
- Raster/RT should now be much closer for direct directional and point lights, especially on textured scenes where sRGB base color was the largest mismatch.

## Verification

- `xmake build -y vultra-app`
  - Passed.
- `git diff --check`
  - Passed.
