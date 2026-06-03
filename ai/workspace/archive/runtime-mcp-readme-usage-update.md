# Runtime MCP README Usage Update

Date: 2026-06-02

## Summary

- Added root `README.md` usage for Runtime MCP/RPC, `visible/offscreen/none`
  render modes, high-FPS MJPEG stream start/stop, browser preview URL, and
  Python client stream consumption.
- Fixed `xmake run vultra-app` examples to pass arguments directly instead of
  inserting a literal `--` separator, which this project launch path forwards to
  `vultra-app`.
- Updated `tools/python/vultra_client/README.md` with offscreen stream usage.
- Updated `ai/README.md` Runtime MCP startup command.
- Added cross-platform `curl`, Windows `cmd.exe`, browser-open, PowerShell, and
  Python examples so Runtime MCP stream usage is not PowerShell-only.
- Compressed the root README Runtime MCP section so it documents the main path
  without becoming a command dump.
- Clarified that `maxHeight=540` is a high-FPS preview recommendation, not a
  stream resolution limit; native-resolution MJPEG is supported but costs more
  CPU, browser decode work, and bandwidth.
- Cleaned stream status semantics so `sourceWidth/sourceHeight` represent the
  original backbuffer size while `width/height` represent the MJPEG output size.
- Re-expanded shell examples into readable multi-line commands. PowerShell uses
  structured hashtables, POSIX shells use heredoc JSON, and `cmd.exe` writes a
  readable JSON request file before posting it with `curl`.
- Re-expanded the Runtime MCP launch commands so each important flag is visible
  on its own line.

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: first retry hit `LNK1104` because a running
  `vultra.exe` held the output file; after stopping that process, the build
  passed.
