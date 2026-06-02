# AI Runtime MJPEG Stream Buffered Encoder

Date: 2026-06-02

## Summary

- Changed `vultra.render.stream` from synchronous main-thread JPEG publishing to
  a latest-frame raw buffer with a background encoder thread.
- Added `capturedFrameCount`, `droppedFrames`, `queuedFrames`,
  `sourceWidth/sourceHeight`, and output `width/height` status fields.
- Added `maxWidth` and `maxHeight` stream caps that preserve aspect ratio.
- Updated `tools/python/vultra_client` so `Simulation.start_stream()` accepts
  `fps=0`, `jpeg_quality`, `max_width`, and `max_height`.

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: passed.
- Python client smoke:
  - `Simulation.start_stream` signature includes `max_width` and `max_height`.
- Runtime smoke:
  - Started `vultra-app` on MCP port `8875` with:
    `xmake run vultra-app --rpc --mcp-port 8875 --project example.vproject --render-mode offscreen --no-xr`
  - Started stream with `fps=0`, `jpegQuality=70`, `maxHeight=720`.
  - Downloaded 3 seconds of MJPEG data to `.vultra/mcp/stream_smoke_8875_720p.bin`;
    file size was `7118905` bytes.
  - Status after startup reported `377` encoded frames over `11.99s`,
    output size `960x720`, `droppedFrames=0`, `queuedFrames=0`.
  - 3-second delta at 720p reported about `29.38` encoded/captured fps.
  - Restarted stream with `jpegQuality=65`, `maxHeight=540`.
  - 3-second delta at 540p reported about `29.83` encoded/captured fps,
    output size `720x540`, `droppedFrames=0`, `queuedFrames=0`.

## Notes

- The encoder is no longer the observed bottleneck in this smoke run: captured
  and encoded frame counts match, and the raw-frame queue does not back up.
- The remaining ~30fps ceiling appears to come from the runtime/offscreen frame
  pacing or synchronous GPU readback/present path. Next high-FPS work should
  inspect offscreen swapchain/present mode, runtime loop timing, and async GPU
  readback rather than further tuning JPEG quality alone.
- For mainstream high-throughput training, this MJPEG path should stay a
  browser/debug preview transport. The training data plane should move toward
  binary/shared memory and then GPU interop or hardware video encoding after
  profiling.
