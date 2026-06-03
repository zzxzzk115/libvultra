# AI Runtime MJPEG Stream Uncapped Mode

Date: 2026-06-02

## Notes

- `vultra.render.stream` now treats `fps=0` or omitted `fps` as uncapped:
  capture attempts run every available engine tick.
- Positive `fps` values still cap MJPEG production and are clamped to
  `1..240`.
- Python `Simulation.start_stream()` now defaults to `fps=0`.
- Full-resolution backbuffer MJPEG is still limited by synchronous GPU readback
  and JPEG encoding. In the `example.vproject` offscreen smoke at `1536x1152`,
  uncapped mode produced roughly 14-16 fps.

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: passed.
- Python signature smoke confirmed `Simulation.start_stream(fps=0, ...)`.
- Started:
  `vultra.exe --rpc --render-mode offscreen --project example.vproject --no-xr
  --mcp-port 8873`.
- Started `vultra.render.stream` with `fps=0`, `jpegQuality=75`.
- Status after about 3 seconds: `frameCount=48`, `fps=0`, `1536x1152`,
  no `lastError`.
- Browser was opened to the uncapped stream URL.

## Handoff

- For higher true FPS, the next implementation should move JPEG encoding to a
  worker queue and then replace synchronous readback with asynchronous
  double/triple-buffered GPU readback. A downscale/max extent option would also
  help browser preview and low-latency embodied AI observations.
