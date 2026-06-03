# Docs Cross-Shell Command Examples

## Scope

- Updated Runtime MCP/RPC launch examples in `README.md` and `ai/README.md` to
  use single-line commands that are copy-pasteable across common shells.
- Replaced shell-specific heredoc request examples with shared JSON bodies plus
  both `curl`/`curl.exe` and PowerShell `Invoke-RestMethod` send commands.
- Added an `AGENTS.md` note that multi-line command examples must use
  shell-appropriate continuations, and should prefer single-line commands when
  cross-shell copy-paste matters.

## Verification

- Searched repository Markdown for bash heredoc JSON-RPC examples and
  backslash-continued Runtime MCP commands.
- Confirmed remaining shell-specific examples are explicitly scoped, such as
  Linux/macOS URL opening or `cmd.exe` examples using `^`.

## Handoff

- No engine source or generated files were changed.
- Existing unrelated worktree change in
  `builtin/generated/include/builtin_rendergraphs.hpp` was left untouched.
