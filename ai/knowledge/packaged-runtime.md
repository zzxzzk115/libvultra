# Packaged Runtime Rules

Packaged runtime (`loadFromVPK=true`) treats the mounted VPK registry and VFS as
the source of truth. Runtime code must not discover project content by walking a
physical `res://` path.

Rules:

- Do not recursively scan `assetService->resolveUri("res://")` in packaged
  runtime. In VPK mode this may resolve to a root-like path such as `/`, causing
  hangs or permission failures.
- Runtime graph, shader, scene, and script discovery should use
  `IAssetService::registry()` and load content through `loadTextAssetSync` /
  `loadBinaryAssetSync`.
- Avoid opening the same VPK a second time during runtime startup. The
  `AssetSystem` already opens and mounts it; use the service registry data.
- Editor-only physical scans are allowed, but they must use
  `std::filesystem::directory_options::skip_permission_denied`, increment with
  `std::error_code`, clear recoverable errors, and avoid treating one denied
  path as a fatal scan failure.
- Single-exe builtin runtime assets should use platform binary resources, not a
  copied `builtin/` folder and not generated C++ byte-array headers for large
  assets. Current desktop paths are Windows `RCDATA` and Linux/macOS assembler
  `.incbin` sections.
- Android and WASM do not use the desktop `vultra-app` single-exe path; use
  their platform asset/VPK mechanisms instead.

Red flags during review:

- `recursive_directory_iterator(assetService->resolveUri("res://"))`
- `openVpk(...)` outside initial asset-system setup or explicit manifest/tool
  inspection
- direct reads from `builtin/textures` without a packaged fallback
- file discovery in runtime startup before the first scene load
