# VS Code Workspace Settings Handoff

Date: 2026-05-27

## Change

- Added `ai/knowledge/vscode-workspace.md` documenting the default
  `.vscode/settings.json` entries for clangd, clang-format, and xmake debug
  target arguments.
- Documented the command-line clangd check pattern:
  `clangd --enable-config --check=<source-file> --compile-commands-dir=.vscode`.
- Documented the command-line clang-format pattern:
  `clang-format -style=file -i <source-or-header-file>`.
- Documented the expectation that C++ source files represented in
  `.vscode/compile_commands.json`, excluding `external/` and `builtin/`, should
  keep clangd and clang-format pass rates above 90%, with remaining failures
  explained.
- Clarified that full-tooling baselines should derive file lists from
  `.vscode/compile_commands.json` with path filters, not broad repository globs.
- Clarified that `clangd --check` is single-file and should be used for focused
  diagnostics unless an explicit resumable batch workflow is requested.
- Investigated using the xmake `llvm` host package for clangd/clang-format and
  decided not to make it a default requirement because the package is too large.
- Created a sibling local repository skeleton at `../llvm-tools` for a possible
  lightweight xmake-repo package that publishes only clangd and clang-format
  release assets.
- Created and pushed the public GitHub repository
  `https://github.com/zzxzzk115/llvm-tools`.
- Published the `21.1.0` release with lightweight Windows x64 and Linux x64
  clangd/clang-format assets.
- Formatted all 194 CDB-listed C++ source files excluding `external/` and
  `builtin/` with the repository `.clang-format`.
- Updated `ai/knowledge/README.md` to include the new knowledge file.

## Verification

- Documentation-only change; no build required.
- Confirmed the existing local `.vscode/settings.json` already contains the
  requested `clangd.arguments`, `clang-format.executable`, and
  `xmake.debuggingTargetsArguments` examples.
- Verified that `clangd --enable-config --check=source/vultra/src/function/openxr/xr_headset.cpp --compile-commands-dir=.vscode`
  reads `.vscode/compile_commands.json` and applies the repository `.clangd`
  config.
- Confirmed the repository has a root `.clang-format` file and the configured
  LLVM `clang-format.exe` exists locally.
- CDB-scoped clang-format dry-run, excluding `external/` and `builtin/`, now
  passes 194/194 files: pass rate 100%.
- `clangd --enable-config --check=source/vultra/src/function/openxr/xr_headset.cpp --compile-commands-dir=.vscode`
  still reports only clangd tweak self-test failures around `XR_FAILED(result)`,
  with no source-level C++ diagnostics observed.
- Previous CDB-scoped clang-format baseline before bulk formatting was 87/194
  passed, 107 failed, pass rate 44.85%.
- A naive full clangd per-file loop was started and interrupted because it is
  too slow for normal task flow. Future full clangd baselines should use an
  explicit batch workflow with path filters and resumable/cached results.
- `xmake build -y vultra-app` passed after formatting.
- `xmake require -y --info llvm` showed an available LLVM 21.1.0 package whose
  Windows archive contains `bin/clangd.exe` and `bin/clang-format.exe`.
- The LLVM package archive is about 1.44 GB on Windows, so this is not suitable
  as a default developer tooling dependency.
- The `../llvm-tools` skeleton includes an xmake package manifest and a GitHub
  Actions workflow that extracts only `bin/clangd` and `bin/clang-format` from
  upstream LLVM archives for Windows, Linux, macOS x64, and macOS arm64.
- LLVM 21.1.0 official release assets include Windows and Linux binaries, but
  no official macOS binary archive was found, so the current workflow publishes
  Windows x64 and Linux x64 only.
- The published `llvm-tools 21.1.0` xmake package was tested from a temporary
  consumer project and installed successfully on Windows.
- The installed package binaries report `clangd version 21.1.0` and
  `clang-format version 21.1.0`.

## Notes

- Future agents should create `.vscode/settings.json` with the documented
  clangd defaults when it is missing.
- Future command-line clangd checks should pass `--enable-config`; this keeps
  the check aligned with repository `.clangd` behavior.
- Future command-line clang-format runs should use `-style=file` so the root
  `.clang-format` is honored.
- Future handoffs should report whether clangd and clang-format pass rates for
  compile-database C++ source files, excluding `external/` and `builtin/`, are
  above 90%, and explain any remaining failures.
- Future tooling checks should avoid broad file globs; use
  `.vscode/compile_commands.json` as the authoritative file list.
- Do not require the full xmake `llvm` host package by default just for
  clangd/clang-format; developers should provide their own local LLVM tooling.
- If a lightweight package is desired later in libvultra, use
  `add_repositories("llvm-tools https://github.com/zzxzzk115/llvm-tools.git")`
  and require `llvm-tools 21.1.0` as a host binary package. macOS support still
  needs a packaging source or build workflow.
- High-priority clangd recommended fixes should be considered early during
  implementation and review.
