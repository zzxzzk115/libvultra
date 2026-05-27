# Graph Layout Rules

## Goal

Graph editors should default to a readable data-flow layout instead of relying
on hand-authored node coordinates.

## Rules

- Lay graphs left to right by dependency depth.
- Keep terminal/output nodes in the final column.
- Keep leaf parameter/source nodes next to their consumer instead of collapsing
  every constant into the first column.
- Sort source nodes feeding the same consumer by the consumer input pin order, so
  property nodes line up with their target slots.
- Propagate that target input lane back through upstream chains, so helper nodes
  feeding `Mix.t` stay below helper nodes feeding `Mix.a` and `Mix.b`.
- Place nodes by inherited port lanes rather than by per-column centering.
- Account for source output and target input pin offsets inside each node, so
  layout aligns edge anchors rather than only node rectangles.
- Account for estimated node height when resolving same-column collisions,
  especially render graph passes with many editable parameters.
- Prefer spacious render graph layouts over compact ones; pass nodes often have
  large parameter panels, so dense layouts quickly become unreadable.
- Align sink/output nodes to their first incoming lane instead of averaging all
  incoming lanes, so the primary output path stays visually straight.
- Give the final sink/output column extra horizontal spacing because output
  nodes are wider and otherwise visually crowd their upstream property nodes.
- Resolve same-column overlaps by nudging nodes downward while preserving lane
  order.
- Put unrelated or cyclic fallback layouts in stable asset order.

## Changes

- Added shared `editor_app/ui/graph_layout` utility.
- Material Graph now has an `Auto Layout` toolbar action.
- Render Graph now has an `Auto Layout` toolbar action using the same layered
  layout rules.
- Material Graph supports right-click add/delete and `Ctrl+S` save.
- Render Graph add menu separates project pass descriptors from builtin passes.
- Re-laid out the default material graph resource with the new rule style.

## Verification

Latest verification should replace this section instead of appending repeated
history.

- Current: `xmake build -y vultra-app` passed after increasing render graph
  auto layout spacing.
