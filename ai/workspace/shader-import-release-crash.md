# Shader Import Release Crash

## Symptom

- Release `vultra.exe` crashes with `0xc0000005` while importing
  `resources/shaders/project.vshaderlib.lua` or running the shader CLI path.
- Debug `vultra.exe shader compile ...` and debug asset import both succeed.
- Standalone packaged `vshaderc` executables also compile the same shader.

## Dump Notes

- Windows Error Reporting recorded:
  `C:/Users/sc23kz/AppData/Local/CrashDumps/vultra.exe.22536.dmp`.
- Event log fault:
  - Exception: `0xc0000005`
  - Fault offset: `0x831509`
  - Module: `vultra.exe`
- Current release map resolves the fault address to a folded `std::_Hash`
  destructor. The faulting instruction reads `[rcx+8]` with `rcx == 0`.
- Stack words near `rsp` point into SPIRV-Cross/vshadersystem reflection:
  - `vshadersystem:reflect.cpp.obj`
  - `spirv-cross-core:spirv_parser.cpp.obj`
  - `spirv_cross::Parser::parse`

## Root Cause

The failure is not a stale `vshaderc` CLI path. Project shader import uses the
C++ API path through `vasset::VAssetImporter` and
`vshadersystem::build_multiple_shaders`.

The crash was caused by binary/toolset integration for the packaged
`vshadersystem` static library:

- The app is currently built with MSVC 19.42 / toolset 14.42.
- `vshadersystem v0.10.0` is resolved from
  `vshadersystem-prebuilt-v0.10.0-windows-x64-msvc-14.29-mt.zip`.
- This mixes a 14.42 release executable with a 14.29 static prebuilt library
  that uses STL-heavy shader reflection code.
- Attempting `xmake f -m release -p windows -a x64 --vs_toolset=14.29 -y`
  failed because this machine does not have the 14.29 toolset installed.
- `xmake f ... --policies=package.precompiled:n` plus
  `xmake require --build --force -y vshadersystem` still resolved the installed
  release `vshadersystem` package to the same `83d6...` directory and still
  reported the `msvc-14.29-mt` prebuilt archive.

The final fix belongs in the `vshadersystem` package recipe: only select a
Windows prebuilt when the active MSVC toolset matches a known prebuilt bucket.
Unknown toolsets must fall back to source build instead of silently consuming
the 14.29 archive.

## Additional Checks

- `vasset-cli.exe import build/.tmp/shader-import-smoke/resources --reimport`
  crashes at the same point as `vultra.exe asset import`, immediately after:
  `shaderlib: shaders/project.vshaderlib.lua`.
- `vultra.exe shader compile -i ... -o ... -S vert --no-cache --verbose`
  also exits with `-1073741819`.
- Building local sibling `../vshadersystem` in release with the current MSVC
  19.42 toolset succeeds.
- The local source-built `vshaderc.exe build --shader_root ... -o ...`
  successfully builds the same smoke shader library.

This means the shader source/manifest and SPIRV-Cross code path are not
intrinsically broken. The crash is tied to the package/prebuilt static library
that libvultra links, not to editor startup or asset registry state.

## Separate Build Hygiene Issue

`builtin/xmake.lua` configures target `vshadersystem` with `MD/MDd` on Windows:

```lua
vshadersystem_configs.runtimes = is_mode("debug") and "MDd" or "MD"
```

The rest of the Windows engine build uses `MT/MTd`. This is a separate ABI/CRT
consistency risk and should be aligned even though `vasset-cli` can reproduce
the crash without the builtin target.

## Mitigation Already Applied

- Editor startup asset scans now allow ordinary import scans but skip
  `.vshaderlib.lua` compilation.
- Explicit shader reimport still keeps shader library import enabled.

## Fix Applied

- Updated `../xmake-repo/packages/v/vshadersystem/xmake.lua` after pulling the
  latest `v0.10.0` recipe. `_windows_prebuilt_asset()` now maps only known
  Windows buckets:
  - MSVC 14.29 -> `windows-x64-msvc-14.29-{md,mt}`
  - MSVC 14.44 -> `windows-x64-msvc-latest-{md,mt}`
  - other/unknown MSVC toolsets -> source build fallback
- Aligned Windows `vshadersystem` runtime config to `MT/MTd`.

The temporary project-local `vshadersystem-local` package was removed after the
upstream package recipe was corrected.

## Verification

- `xmake l scripts/test.lua --shallow --precompiled -vD -p windows -a x64 -m release --runtimes=MT "vshadersystem v0.10.0"`
  passed in `../xmake-repo` on MSVC 14.42. It did not select
  `windows-x64-msvc-14.29-mt`; it fell back to source install.
- `xmake l scripts/test.lua --shallow -vD -p windows -a x64 -m release --runtimes=MT "vshadersystem v0.10.0"`
  passed in `../xmake-repo`.
- `xmake build -y vasset-cli` passed.
- `xmake build -y vultra-app` passed.
- `build/windows/x64/release/vasset-cli/vasset-cli.exe import build/.tmp/shader-import-smoke/resources --reimport`
  passed with `LASTEXIT=0`.
- `build/windows/x64/release/vultra-app/vultra.exe asset import build/.tmp/shader-import-smoke/resources --reimport`
  passed with `LASTEXIT=0`.
- `build/windows/x64/release/vultra-app/vultra.exe shader compile -i resources/shaders/fullscreen/fullscreen_triangle.vert.vshader -o build/.tmp/vultra-vshaderc-test -S vert --no-cache --verbose`
  passed with `LASTEXIT=0`.
- `xmake build -y vasset-example-vpk` passed and ran the C++ import/pack path.

## Next Checks

- If the editor still crashes after this fix, investigate runtime renderer or
  asset loading separately; shader import is no longer reproducing the release
  crash.
- If source build is still too expensive on common developer machines, add a
  small number of explicit Windows prebuilt buckets instead of defaulting all
  unknown MSVC versions to 14.29.
