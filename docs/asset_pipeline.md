# Asset Pipeline

Source assets and derived data have separate lifetimes. Importing never rewrites a glTF, GLB, OBJ, MTL, FBX, DDS, image or buffer. Original filenames and relative references stay intact. Derived data is disposable and defaults to the ignored `.vultra/assets/` directory.

## Runtime Entry Point

```cpp
#include <vultra/function/asset/asset_pipeline.hpp>

vultra::AssetImportOptions options;
auto asset = vultra::importAsset("resources/models/Sponza/Sponza.gltf", options);
vultra::GpuScene gpu(device, asset);
```

`ImportedAsset::scene` owns static geometry and materials. `textures` owns upload-ready mip chains and seven texture indices per material: base color, metallic/roughness, normal, occlusion, emission, specular weight and specular color. Decoded source images are released after preparation. `GpuScene` uploads those prepared bytes without regenerating mips or recompressing them. Procedural callers can continue constructing `GpuScene` directly from `Scene`.

The entry point is synchronous: it returns after its internal `vtask` jobs have joined. It has no registry, asset handles or import plugins. Scene replacement happens between frames; the viewer builds new GPU resources before replacing its current model. A failed model load keeps the previous scene visible.

## Build-Time Preparation

Building a model example first builds the CPU-only `vultra-import` tool and prepares its default source in the repository's `.vultra/assets/` cache. `asset-damaged-helmet` is shared by Debug Draw and glTF Viewer; `asset-sponza` is shared by desktop, mesh-shading and OpenXR Sponza. Ray Query and Cornell Box use `asset-rayquery` and `asset-cornell-box`. The shared dependencies run once per build, and simple examples do not import unrelated models. `xmake build example-assets` prepares all four sources explicitly. Import progress remains visible during the build.

An unchanged build checks the archive checksum, import recipe and dependency content hashes without parsing geometry, decoding images or baking textures again. Cache deletion, corruption, source changes and recipe changes trigger preparation. Build-time cache publication failure fails the build, so a successful asset step means the cache is usable. `vultra-import --reimport` still forces regeneration.

At runtime, `importAsset()` restores the prepared derived data. Missing or invalid entries, custom model paths and changed import options fall back to the existing importer with progress logs. Builds and normal `xmake run` use the same project-root cache; custom working directories or `--cache-dir` select their own cache and may import on first use.

This moves mip generation and BC7 encoding out of normal startup. Original geometry/material parsing, source image levels, GPU uploads, meshlet construction and HDR/IBL setup still happen at runtime. They are not silently duplicated into the derived texture cache. Content validation remains enabled, including edits that preserve file size and modification time.

## Import and Cache

1. Resolve the source file and import recipe.
2. Load geometry, materials and source images with TinyGLTF, TinyObjLoader or OpenFBX; record the content of every consumed file.
3. Try the matching `.vasset` file; verify its checksum, version, recipe and dependencies against the loaded source content.
4. Restore generated mip levels and BC7 blocks on a hit. On a miss, prepare color-aware mips and optional BC7 data textures in independent `vtask` jobs.
5. Before publishing a new cache, verify that dependencies still match the consumed bytes, then replace the archive atomically.

Dependencies include external `.bin`, PNG/JPEG, DDS, FBX texture and MTL files, including files outside the model directory and percent-encoded glTF URIs. Embedded GLB/data-URI content is covered by the source file. Content hashes detect changes even if file size and modification time are unchanged. Unrelated neighboring files do not invalidate an asset.

Cache identity includes the canonical source path, pipeline version, mip option, compression mode and encoder recipe. The payload contains **only generated mip levels, newly encoded BC7 blocks and their texture-slot/source metadata**. Geometry, materials, source image base levels and all authored DDS mips are read from the original files on every load. Even DDS color-space expansion is repeated from the source rather than persisted as another full texture. A model with no generated texture data leaves only a small metadata record.

This avoids duplicating existing assets on disk. A warm load still parses geometry and decodes source images; it skips generated mip filtering and BC7 encoding. Generated geometry attributes are currently rebuilt along with the mesh. The cache is local derived data, not a portable distribution format. Moving a checkout creates a new cache identity.

Corrupt, outdated or stale caches are logged and rebuilt. Import errors remain errors and leave the previous cache intact. A cache write failure is logged while the successfully imported in-memory asset remains usable. Concurrent writers publish complete files through temporary files and atomic replacement.

Increment `kPipelineVersion` in `asset_pipeline.cpp` whenever importer output, layouts, mip filtering, dependency handling or compression behavior changes. Old entries can be removed manually; automatic eviction and package archives are outside the current scope.

## Texture Policy

- Base color and emission retain RGBA8 sRGB textures. Mip filtering averages linear light and encodes it back to sRGB.
- Mips use the box filter in `stb_image_resize2`, including its SSE2 path on Windows x64 and lookup-based sRGB conversion. Alpha stays linear; packed data channels are filtered independently. Odd-sized images include their edge texels.
- Linear data textures use RGBA8 UNORM or BC7 UNORM. A shared source image used as both color and data receives separate prepared textures.
- Specular color is sRGB; specular weight uses linear alpha, following `KHR_materials_specular`. BC5 normal maps reconstruct positive tangent-space Z in the shader; uncompressed import retains two channels as RG8 for the same reconstruction.
- BC7 baking uses the public `bc7enc_rdo` ISPC encoder with its fast preset. SSE2/AVX2 dispatch encodes linear RGBA directly to BC7 without a UASTC intermediate. See [encoder source and license](../external/bc7enc/README.md).
- VRI v0.1.17 exposes BC7 UNORM but no BC7 sRGB format. Color textures therefore retain hardware sRGB filtering; the pipeline does not apply an approximate shader decode after gamma-space filtering.
- Images smaller than one 4x4 block remain RGBA8. Odd dimensions and the final small mip levels are supported by block-aware uploads.
- DDS preserves authored mip levels; `--no-mipmaps` retains only level zero. Native linear BC1/2/3/4/5/6H unsigned/7 blocks are retained without recompression. R8, RG8, RGBA8, BGRA8 and RGBA16F/32F are also accepted. Unsupported signed, integer and packed formats are rejected.
- `loadDds()` honors DDS color-space metadata. Material slots override that interpretation consistently with glTF semantics. BC color data is decoded to RGBA8 sRGB because VRI does not expose BC sRGB formats. DirectXTex performs CPU loading/decompression with its D3D11/D3D12 support disabled.
- `--compression none` decompresses source DDS blocks as well as disabling generated BC7. It provides an uncompressed research/reference path. Source files remain identical with either setting.

HDR environment decoding and IBL precomputation still belong to `Environment` and are not persisted by this model cache. OBJ support currently covers static geometry and untextured MTL diffuse/specular/emission data. There is no asset hot reload, mesh optimization, animation import, texture streaming or virtual filesystem.

## FBX and DDS Scope

`loadFbx()` imports static mesh instances, hierarchy/geometric transforms, signed axis metadata and unit scale into meters with Y up and -Z forward. Mirrored transforms reverse winding; supplied normals use the inverse transpose, and absent normals become face normals. OpenFBX 0.9 supports triangles and convex polygon fans here; triangulate concave polygons when exporting. Skinning and blend shapes are rejected. Animation is logged and ignored.

FBX materials map diffuse/emission factors, a shininess-to-roughness approximation, and diffuse/normal/emission texture slots. Other texture slots produce a warning; this does not claim complete FBX PBR material equivalence. Binary embedded images and external PNG/JPEG/DDS images are supported. ASCII embedded base64 images are rejected. Relative texture paths remain relative to the FBX file.

DDS can be loaded directly with `loadDds()`, through FBX texture references, or through glTF `MSFT_texture_dds`. `loadSceneImage()` retains DDS source bytes until the material slot determines the transfer function. The current upload path accepts one 2D texture and its mips. Arrays, cubemaps, volumes and premultiplied alpha are explicitly unsupported; environment cubemaps still use the existing HDR-to-IBL path.

## Jobs and Progress

Image decoding, FBX array parsing, geometry and texture preparation, dependency verification and cache payload restoration share a small internal `runImportJobs()` helper backed by `vtask::TaskSet`. `--import-jobs` / `AssetImportOptions::workers` limits all these phases. The worker limit is the minimum of hardware concurrency, job count, the requested limit and a memory estimate based on half the currently available physical RAM.

glTF jobs process primitive instances; OBJ jobs process shapes; FBX jobs process mesh material partitions. Vertex/index ranges are allocated before dispatch. Each job fills its own range, generates missing normals and computes local bounds. glTF shared-vertex normals accumulate triangles in their original order inside each job, avoiding floating-point atomics and nondeterministic reductions. OBJ preserves its existing face-normal policy and supplied normals. Final material groups and bounds are merged in source order. glTF parsing retains encoded images, which are decoded in independent jobs afterwards. FBX uses OpenFBX's job callback for array parsing and postprocessing, then reads/decodes independent textures through vtask. Dependency observers are called serially even when a file is consumed by a worker. glTF/OBJ parsing, hierarchy traversal and reading the single cache archive remain serial; a single primitive, shape or FBX partition remains one geometry job. Cache metadata is indexed serially into bounded byte views; jobs restore source levels and generated mip payloads before those views expire.

Identical image contents with matching dimensions and color space share one texture job, even when they have different filenames. Hash matches are confirmed by comparing the actual bytes. Jobs write to fixed output slots, so completion order cannot change materials or cached texture bytes. Texture jobs are sorted by size before dispatch. Texture scratch is estimated at 16 MiB plus twice the largest decoded RGBA image size per worker; this is a concurrency heuristic, not a hard allocation limit. Geometry jobs write directly into preallocated arrays; their scratch estimate includes two vector arrays for generated tangent frames. glTF and FBX generate missing tangents in these same jobs, with deterministic accumulation and no nested pool. Generated geometry attributes are rebuilt from source rather than persisted by the texture cache.

Each texture job feeds bounded batches of blocks to the SIMD encoder without a nested thread pool. Encoder tables are initialized once before concurrent encoding. Worker exceptions stop further work, join running jobs and propagate to the caller; failures never produce a partial cache or silently disable compression.

Mesh-enabled `GpuScene` construction runs meshoptimizer per primitive through the same vtask helper, respecting the viewer's `--import-jobs` limit and logging progress. It inherits `dev`'s 64-vertex / 124-triangle clusterization and preserves material boundaries. It reorders triangle references without modifying source vertices, normals, tangents or UVs. Meshlet buffers and bounding spheres are currently rebuilt in memory when the scene is uploaded; they are not stored in the texture-only `.vasset` cache.

Info logs show cache validation, parsing, image decoding, cache restoration, completed/total geometry jobs, loaded geometry/image counts, worker count and memory estimate, completed/total textures, dependency verification, cache writing and stage timings. Completion logs are throttled to about twice per second, with the first and last completion always shown. Debug logging identifies each texture and reports separate mip/encoding times. A long individual job can remain between completion updates; the API does not yet pump the application's UI during import.

`--import-jobs 1` is useful for profiling or limiting CPU use. Worker count affects execution only and does not invalidate the cache.

Cache serialization reserves its final size and writes the header, metadata and payload into one buffer. The checksum is filled in place before atomic publication, avoiding a second full archive copy. Cache restoration still reads the archive into memory and verifies its checksum before dispatching independent payload copies; it is not a streaming loader.

## Tools and Viewer

```powershell
xmake build example-assets
xmake build example-sponza
xmake build vultra-import
xmake run vultra-import resources/models/Sponza/Sponza.gltf
xmake run vultra-import resources/models/Sponza/Sponza.gltf --reimport
xmake run vultra-import resources/models/Sponza/Sponza.gltf --reimport --import-jobs 1
xmake run example-sponza --cache-dir .vultra/assets --compression bc7-linear
xmake run example-gltf-viewer --no-cache --compression none
```

`vultra-import` prepares models without creating a GPU device and skips loading when its cache is already current. The default run set contains examples; the importer and asset dependencies are not run as examples. Model examples share `--cache-dir`, `--reimport`, `--no-cache`, `--no-mipmaps`, `--import-jobs N` and `--compression none|bc7-linear`.

In the viewer, **Reload** validates and uses the current cache; **Reimport** bypasses it and rebuilds derived data. Loading another model preserves the import options.

`test-asset-pipeline` covers unchanged cache hits, external glTF/MTL dependencies, same-size/same-timestamp source edits, corruption recovery, forced import, option changes, disabled caching, failed import preservation, original source preservation and actual GPU sampling of sRGB and odd-sized BC7 textures. It also checks linear-light/alpha/odd-edge mip filtering, content deduplication, identical serial/parallel geometry and texture output, shared-vertex normals, transformed mesh instances, OBJ material grouping and worker failure propagation/recovery. Cache size checks ensure that source base levels are not serialized, with identical source and generated pixels after restoration.

`test-asset-formats` covers FBX parent/mirrored transforms, units, triangulation, missing normals, DDS material references, parallel determinism, source preservation and failed image import recovery. It also checks DDS authored mip retention without duplicated cache payloads, compressed linear versus sRGB output, BC5 decompression, glTF DDS source selection, malformed/truncated DDS, rejected arrays, content-based dependency invalidation, cache restoration and Vulkan sampling.
