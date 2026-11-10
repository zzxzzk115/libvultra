# Local Dependency Recipes

These local xmake recipes build pinned dependencies for the player and separate research editor. They do not vendor upstream source into this repository.

- `slang-static` fetches [Slang](https://github.com/shader-slang/slang) tag `v2026.11`, tag object
  `03f76f6c91fd4c6db1c067ee92ad1af188d302b6`, commit `7432ffa4ea189571a498d481e82f9d8ce337e20b` (MIT).
  It builds the compiler and embedded standard library without shared helper modules.
  `constant_bool_cast.patch` adds the missing integer-constant-to-bool fold in
  [TypeCastIntVal](https://github.com/shader-slang/slang/blob/03f76f6c91fd4c6db1c067ee92ad1af188d302b6/source/slang/slang-ast-val.cpp#L1928-L1987):
  conditional array lengths otherwise remain symbolic and the reflection API can abort in `getIntVal`.
  The fold uses nonzero truth semantics and introduces no exception handling. Vultra passes linked program context
  to array-count reflection, and fingerprints the recipe and patch in its native shader identity. Existing cached
  compiler packages need rebuilding after recipe changes. The source recipe supports macOS arm64/x86_64 as well as
  the existing Windows/Linux hosts; the material-block qualification rebuilt the arm64 package from source.
  The recipe adds `<climits>` to `slang-parser.cpp` because that release uses `INT_MIN` without the header under
  GCC 16. It also omits the excluded-from-build GLSL module from installation: the upstream static install target
  otherwise expects a helper library that was never built. The recipe removes the GLSLang, SPIRV-Opt and SPIRV-Dis
  downstream locator registrations because their shared helper module is omitted; direct SPIR-V emission otherwise
  reports a failed optional module load on every compilation. The recipe installs and links Slang's internal
  `compiler-core`, `core`, LZ4 and cmark archives, which upstream's static install does not place beside
  `libslang-compiler.a`.
- The shader compiler feature also links [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools/tree/vulkan-sdk-1.4.335.0)
  `1.4.335+0` (Apache-2.0) statically through the pinned xmake-repo recipe. Its standard performance pipeline
  finishes direct Slang SPIR-V for default/high/maximal modes, preserving reflected bindings and interface.
  The existing `none` mode bypasses these transforms. The linked library identity participates in shader cache keys.
  Cooked-only consumers need neither this compiler library nor an external optimizer executable.
- `openxr-static` fetches [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) release `1.1.49` (Apache-2.0; tarball hash is pinned in the recipe). It builds the loader with vendored JsonCpp rather than the system target, which cannot be exported by the static SDK build on Linux.
- `openxr-android` builds the same OpenXR-SDK release with a local patch for runtime discovery. The Android loader's
  probe opens and immediately closes the active runtime before opening it again for the instance. Some runtimes
  register thread-local cleanup during that close, leaving a callback to unloaded code. The patch keeps probed runtime
  libraries mapped until process exit using `RTLD_NODELETE`; XR instances and sessions are still destroyed normally.
  This also means changing the active runtime may retain previous runtime libraries for the life of the process.
- `moltenvk-vultra` builds the macOS Vulkan ICD from [MoltenVK 1.2.11](https://github.com/KhronosGroup/MoltenVK/tree/81541f6488c5feeace53867fc8ab612e578766da)
  (Apache-2.0). Its SPIRV-Cross, Vulkan-Headers and cereal resources retain upstream's fixed revisions and archive
  checksums. The SPIRV-Cross patch backports only `to_buffer_size_expression()` from
  [upstream commit 896fc880](https://github.com/KhronosGroup/SPIRV-Cross/commit/896fc8807f985c2c1b941f07d2f7a8cd1ca1752a):
  preserve member accesses inside the array index while forming the buffer-size identifier. A second include-only
  patch uses glslang's installed public `ShaderLang.h`. The auxiliary GLSL converter and SPIR-V diagnostics link the
  pinned `glslang`/`spirv-tools` 1.4.335+0 packages statically; the runtime does not depend on SDK compiler libraries.
  The small CMake build uses Apple Command Line Tools and mirrors the pinned upstream macOS source set. The generated
  revision incorporates translator, converter and build hashes for pipeline-cache compatibility. `xmake run` selects
  its package-local ICD; direct launches must set `VK_DRIVER_FILES` explicitly. Existing SDK files remain unchanged.

The system Vulkan loader, native GPU driver, display stack and libc remain external dependencies; the macOS Vulkan ICD
above is supplied by the project package. Project native script modules are VPK payloads, loaded separately from the
statically linked engine.

`imgui-node-editor` is editor-only, statically built at commit [`021aa0ea4da13fed864bafb2a92d4c5205076866`](https://github.com/thedmd/imgui-node-editor/tree/021aa0ea4da13fed864bafb2a92d4c5205076866) (MIT). It shares Vultra's pinned docking ImGui build. No upstream source patches are applied; the local recipe supplies an xmake static-library target. Neither `vultra` nor `vultra-runtime` links the canvas.
