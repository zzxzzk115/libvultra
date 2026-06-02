# AI Runtime MJPEG Stream Async Readback

Date: 2026-06-02

## Summary

- Added non-blocking `CommandBuffer::isComplete()` over Vulkan fence status.
- Changed `vultra.render.stream` readback from synchronous
  `RenderDevice::readTextureBytes()` / `waitIdle()` to a small in-flight
  readback slot ring.
- Moved preview resolution caps before readback:
  - full backbuffer is blitted/downscaled on GPU when `maxWidth`/`maxHeight`
    reduce the preview size;
  - only the smaller preview texture is copied to the readback buffer.
- Stream status now reports `readbackSubmittedCount`,
  `readbackCompletedCount`, and `readbackSkippedCount`.

## Verification

- `git diff --check`: passed.
- `xmake build -y vultra-app`: passed.
- Offscreen smoke on port `8877` with `fps=0`, `jpegQuality=70`,
  `maxHeight=720`:
  - output size: `960x720`
  - readback/capture: about `99.6fps`
  - encoded/published MJPEG: about `85.3fps`
  - skipped readbacks: `0`
- Offscreen smoke on port `8877` with `fps=0`, `jpegQuality=65`,
  `maxHeight=540`:
  - output size: `720x540`
  - readback/capture: about `180.7fps`
  - encoded/published MJPEG: about `148.8fps`
- Final smoke on port `8879` after removing an over-aggressive encoder
  backpressure experiment:
  - output size: `720x540`
  - readback/capture: about `178.6fps`
  - encoded/published MJPEG: about `148.1fps`
  - skipped readbacks: `0`

## Notes

- Compared with the previous synchronous path, 540p preview improved from about
  `29.8fps` to about `148fps` published MJPEG in this scene.
- The remaining bottleneck is CPU JPEG encoding and BGRA/RGBA-to-RGB conversion.
  Higher preview rates should move to hardware video encoding or a browser
  transport that accepts GPU-friendly formats.
