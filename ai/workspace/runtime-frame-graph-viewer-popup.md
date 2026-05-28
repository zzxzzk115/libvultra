# Runtime Frame Graph Viewer Popup

Date: 2026-05-29

## Current State

- Runtime Frame Graph Viewer is opened from the editor Tools menu as an independent modal popup.
- The Render Graph asset window no longer owns or opens the runtime viewer.
- Raw DOT is used only for Graphviz layout and edge routing.
- JSON snapshot data owns runtime node labels, versions, import flags, side-effect flags, and texture lookup metadata.
- Resource labels hide the initial `v1` suffix and show `v2+`.
- Debug texture exact keys include the raw-DOT-compatible resource/version identity (`R<resourceId>_<version>/layer:<n>`).
- Runtime texture previews clean up thumbnail caches, preview overrides, graphnode caches, and texture state when the viewer closes.

## Notes

- Graphviz is built with expat support so HTML/table labels in raw DOT parse correctly.
- Runtime raw DOT is sanitized from `fixedsize=true` to `fixedsize=false` before layout to avoid Graphviz label-size warnings.
- Color/final outputs such as FXAAOutput, Backbuffer, FinalComposition, Tone, LightingOutput, and BaseColor default to gamma-correct preview.
- Depth thumbnails and expanded previews run the same default preview setup, including linear depth mode and auto-fit where applicable.

## Verification

- `xmake build -y vultra-app` passed after the final cleanup.

