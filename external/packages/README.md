# Static Runtime Dependencies

These local xmake recipes keep the runtime's engine dependencies in the executable. They do not vendor upstream source into this repository.

- `slang-static` fetches [Slang](https://github.com/shader-slang/slang) tag `v2026.11`, commit `03f76f6c91fd4c6db1c067ee92ad1af188d302b6` (MIT). It builds the compiler and embedded standard library without shared helper modules. The recipe adds `<climits>` to `slang-parser.cpp` because that release uses `INT_MIN` without the header under GCC 16. It also omits the excluded-from-build GLSL module from installation: the upstream static install target otherwise expects a `.so` that was never built. The recipe also removes the GLSLang, SPIRV-Opt and SPIRV-Dis downstream locator registrations because their shared helper module is omitted; direct SPIR-V emission otherwise reports a failed optional module load on every compilation. The recipe installs and links Slang’s internal `compiler-core`, `core`, LZ4 and cmark archives, which upstream’s static install does not place beside `libslang-compiler.a`.
- `openxr-static` fetches [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) release `1.1.49` (Apache-2.0; tarball hash is pinned in the recipe). It builds the loader with vendored JsonCpp rather than the system target, which cannot be exported by the static SDK build on Linux.

The system Vulkan loader, graphics driver, display stack and libc remain operating-system dependencies. Project native script modules are VPK payloads, loaded separately from the statically linked engine.
