# AI Runtime MJPEG Stream Phase 1

Date: 2026-06-02

## Implementation Notes

- Added `vultra.render.stream` with `start`, `status`, and `stop` actions.
- `start` returns an HTTP MJPEG URL on the same Runtime MCP localhost server:
  `http://<host>:<port>/stream/<id>`.
- The stream endpoint serves
  `multipart/x-mixed-replace; boundary=vultra-frame` with JPEG frames and
  `X-Vultra-Frame` sequence headers.
- Stream capture currently reads the render backbuffer, converts RGBA/BGRA to
  RGB, JPEG-encodes in process via stb, and wakes stream clients.
- Stream clients run on separate server threads so a long GET request does not
  block MCP JSON-RPC calls.
- `render-mode=none` rejects stream start, matching RGB/depth capture behavior.
- The zero-dependency Python client now exposes:
  - `Simulation.start_stream(fps, jpeg_quality)`
  - `Simulation.stream_status()`
  - `Simulation.stop_stream()`
  - `Simulation.iter_mjpeg_frames(url, max_frames)`

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: passed.
- Python import smoke passed:
  `PYTHONPATH=tools/python python -c "from vultra_client import Simulation; ..."`
- Live offscreen smoke:
  - Started `vultra.exe --rpc --render-mode offscreen --project example.vproject
    --no-xr --mcp-port 8871`.
  - `vultra.render.stream(start, fps=5, jpegQuality=70)` returned an MJPEG URL.
  - `vultra.render.stream(status)` reported active stream, `1536x1152`,
    frame count 3, and no `lastError`.
  - `curl --max-time 3 <stream-url>` wrote 2,071,824 bytes before the expected
    timeout.
  - Captured bytes contained `--vultra-frame` and `Content-Type: image/jpeg`.
  - `vultra.render.stream(stop)` passed.
  - `vultra.editor.quit` exited the process with code 0.

## Handoff

- MJPEG gives Vultra a first video transport that can be consumed by browsers,
  Python standard-library code, and OpenCV.
- This is still a backbuffer stream. The next visual AI slice should add
  camera-observation streams backed by named camera render resources, with RGB,
  depth, and later segmentation channels.
- MJPEG is useful for preview and low/medium-rate visual observations. For high
  throughput training, keep the existing roadmap: binary payloads, shared memory,
  then GPU/CUDA interop after profiling.
