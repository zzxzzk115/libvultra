# Fullscreen Close Resize

## Summary

- Prevented pending resize work from running after the window has requested close.
- Clears resize state when a close or quit event is received.

## Root Cause

Closing a fullscreen window can emit a final resize/pixel-size change while the platform transitions out of fullscreen.
If that resize is applied before shutdown, the swapchain can briefly recreate at the restored window size and flash a low-resolution frame.

## Verification

- `xmake build -y vultra-app`
  - Passed.
- `git diff --check`
  - Passed.
