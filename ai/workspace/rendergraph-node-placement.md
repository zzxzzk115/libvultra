# Render Graph Node Placement

Date: 2026-05-31

## Change

- Render graph add-menu creation now records the screen-space position where
  the add popup was opened.
- Newly created pass nodes are placed at that screen position on their first
  frame with `ImNodes::SetNodeScreenSpacePos`, then persisted back into render
  graph editor metadata as grid-space coordinates.
- Toolbar Add still opens the same menu, using the button-adjacent popup
  location as the initial placement anchor.

## Verification

- `xmake build -y vultra-app` passed.
- `git diff --check -- source/vultra_app/src/editor_app/ui/windows/render_graph_window.cpp` passed.
