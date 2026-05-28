# VS Code Workspace Setup

When a developer opens Vultra in VS Code and `.vscode/settings.json` is
missing, create it with the project defaults needed by the common extensions:

```json
{
  "clangd.arguments": [
    "--header-insertion=never",
    "--compile-commands-dir=.vscode"
  ],
  "clang-format.executable": "C:\\Program Files\\LLVM\\bin\\clang-format.exe"
}
```

If an xmake debug target needs runtime arguments, configure
`xmake.debuggingTargetsArguments` in the same file. For the editor sample app,
use:

```json
{
  "xmake.debuggingTargetsArguments": {
    "vultra-app": ["--editor", "--project", "${workspaceFolder}/example.vproject"]
  }
}
```

These settings make the clangd and xmake VS Code extensions useful out of the
box. Treat high-priority clangd diagnostics and recommended fixes as important
signals during implementation and review, especially when they indicate real
compile errors, missing includes, type mismatches, lifetime issues, or broken
API usage.

When reproducing clangd diagnostics from the command line, enable project
configuration explicitly and point clangd at the VS Code compilation database:

```powershell
clangd --enable-config --check=<source-file> --compile-commands-dir=.vscode
```

The `--enable-config` flag is important because repository settings such as
`.clangd` `CompileFlags` are otherwise easy to miss in command-line checks.

Use clang-format from the same LLVM toolchain when formatting C++ files. VS Code
should point `clang-format.executable` at the installed LLVM binary, and
command-line formatting should let clang-format discover the repository
`.clang-format` file:

```powershell
clang-format -style=file -i <source-or-header-file>
```

For review-only checks, omit `-i` or run a diff after formatting. Do not apply
bulk clang-format rewrites to unrelated files.

For all C++ source files represented in `.vscode/compile_commands.json`, excluding
`external/` and `builtin/`, aim for a clangd and clang-format pass rate above
90%. Prioritize fixing high-severity clangd diagnostics and formatting
violations in engine-owned files. If the remaining failures are tool self-test
issues, generated files, or unrelated pre-existing problems, record that
distinction in the handoff notes instead of hiding the failure.

Use the compilation database as the source of truth for full-tooling baselines.
Apply path include/exclude filters there instead of manually scanning the whole
repository with broad globs. In particular, exclude `external/` and `builtin/`
from baseline counts.

`clangd --check` is a single-file diagnostic entry point. It is useful for the
current file, changed files, or a small focused set from the compilation
database. Do not run naive per-file full-repository clangd loops during normal
tasks; reserve full clangd baselines for an explicit batch workflow with cached
or resumable results. For whole-project validation, prefer the relevant xmake
build target as the compile correctness check, then use clangd for editor-style
diagnostics on focused files.

Do not add a default xmake requirement for the full LLVM host package only to
provide clangd or clang-format. The package can provide `bin/clangd.exe` and
`bin/clang-format.exe`, but the Windows archive is very large, so developers
should install or configure their own LLVM/clangd tooling environment instead.
