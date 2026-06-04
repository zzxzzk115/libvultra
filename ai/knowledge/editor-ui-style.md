# Editor UI Style Convention

Stable rules for property/inspector UI in the Vultra editor (`source/vultra_app`,
Dear ImGui). The governing rule: **label on the left, control on the right.** Every
labeled field — in the Inspector, Settings, material panels, and debug windows —
follows the same two-zone layout so the editor reads as one product.

```
┌────────────────────────────────────────────────────┐
│  Label            │  [ control fills the right zone ]│
│  (fixed width)    │                                  │
└────────────────────────────────────────────────────┘
```

## The Rule

- Label text sits in a fixed-width left zone; the control fills the remaining right
  zone (`SetNextItemWidth(-1.0f)`).
- Never rely on ImGui's built-in trailing label (`ImGui::DragFloat("Speed", ...)`
  renders the label on the *right*, breaking the convention). Use a hidden id label
  (`"##speed"`) on the control and draw the visible label yourself on the left.
- Always call `ImGui::AlignTextToFramePadding()` before the label so it lines up with
  the control's baseline.

## Canonical Helpers

Both helpers live in `source/vultra_app/include/editor_app/ui/settings_widgets.hpp`
(`vultra_app::ui`). Use them instead of hand-laying `SameLine`/`Columns`.

### Single rows — `beginPropertyRow` / `endPropertyRow`

```cpp
ui::beginPropertyRow("Intensity");           // default labelWidth = 160.0f
ImGui::DragFloat("##intensity", &intensity, 0.05f, 0.0f, 10000.0f, "%.2f");
ui::endPropertyRow();
```

`beginPropertyRow(label, labelWidth = 160.0f)` pushes an id, draws the aligned label,
`SameLine(labelWidth)`, and `SetNextItemWidth(-1.0f)`. `endPropertyRow()` pops the id.
`beginSettingsRow` / `endSettingsRow` are the settings-dialog flavor (label width 150)
and forward to the same primitive — prefer `beginPropertyRow` in inspector code.

Controls that ignore item width (checkbox, color swatch button) still belong on the
right: the label is drawn left and the control follows after `SameLine`.

### Tables — for dense, multi-row property blocks

When a block has many rows or needs aligned columns/per-row buttons, use a 2-column
table instead of N single rows:

```cpp
if (ImGui::BeginTable("MyProps", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    // per row:
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputXxx("##value", ...);
    ImGui::EndTable();
}
```

This is the pattern already used by the material-properties and node-default-params
panels in `inspector_window.cpp`. Use a fixed 160px label column and a stretch value
column so all windows align identically.

## Indentation

Component field bodies are indented **once, at the dispatch level**, so every
component's labels and controls line up under their collapsing header. In
`inspector_window.cpp` the component loop wraps the whole field block in a single
`ImGui::Indent()` / `ImGui::Unindent()` right after the header opens. Individual field
drawers (`drawLightComponentFields`, `drawTransformComponentFields`, …) must **not**
call `ImGui::Indent()` themselves — doing so double-indents that component relative to
its siblings (the bug that left Light flush-left while Transform was indented). Add a
new component's field drawer with no indent of its own and it inherits the shared one.

## Mechanics Reference

| Need | Use |
| --- | --- |
| Align label to control baseline | `ImGui::AlignTextToFramePadding()` before the label |
| Draw label without format pitfalls | `ImGui::TextUnformatted(label)` |
| Control fills the right zone | `ImGui::SetNextItemWidth(-1.0f)` |
| Suppress ImGui's own label | hidden id label `"##field"` |
| Single field row | `ui::beginPropertyRow` / `endPropertyRow` |
| Dense / multi-column block | `ImGui::BeginTable(2, SizingStretchProp)` + fixed 160px label column |

Standard label width is **160px** (`beginPropertyRow` default, table label column).
Settings dialogs keep 150px via `beginSettingsRow`. Theme colors come from
`vultra::imgui_theme` applied in `settings_widgets.cpp` — do not hardcode colors in
property rows.

## What Not To Do

- ❌ `ImGui::DragFloat("Speed", &v)` / `ImGui::Checkbox("Loop", &b)` with a visible
  built-in label — puts the label on the right and the widths drift per window.
- ❌ Hardcoded `SameLine(120.0f)` + magic widths scattered per call site — use the helper.
- ❌ Mixing `ImGui::Columns` and tables in the same panel.

## Files That Render Property Rows (must conform)

- `source/vultra_app/src/editor_app/ui/windows/inspector_window.cpp` (component fields,
  material panel) — primary surface.
- `source/vultra_app/src/editor_app/ui/windows/settings_windows.cpp` (reference user of
  `beginSettingsRow`).
- `frame_debugger_window.cpp`, `content_browser_window.cpp`, `material_graph_window.cpp`,
  `profiler_window.cpp`, `scene_hierarchy_window.cpp` and the helpers under
  `source/vultra_app/src/editor_app/ui/`.

When adding or editing a property field anywhere in the editor, route it through
`beginPropertyRow`/`endPropertyRow` or the table pattern above. Migrate adjacent bare
controls opportunistically rather than adding a second style next to them.

### Reflected components are already centralized

Most components (Layer, Canvas, the UI components, Environment, ReflectionProbe,
RigidBody, the collision Shapes, …) draw their fields through the generic
`drawMetaFields` / `drawMetaValue` path in `inspector_window.cpp`. That path now wraps
every single-row field in `beginPropertyRow` automatically, so **a new reflected field
conforms with no extra work**. The exceptions are fields that draw their own label plus
a multi-line picker/grid — asset references (`CoreUUID`), the script URI field, and the
render-layer mask/cullingMask fields — which `metaFieldDrawsOwnRow` excludes from the
row wrap. Add a new self-laid-out field type there if it manages its own label.
Hand-written drawers (Transform, Rect, Light, Animator, XR View, UI Layout, Mesh) call
`beginPropertyRow` directly.

### Asset inspectors

The asset/source inspectors (Material source, Material Graph Node, Render Graph Pass,
Texture Import, plus the Mesh material-override block and asset previews) also follow
the rule. Shared helpers carry the layout: `drawMaterialStringInput` draws a property
row, except when given a `##`-prefixed label (a table cell that already drew the label),
in which case it emits only the control. The shader pickers
(`drawShaderOptionSelector`, `drawShaderSelector`, `drawShaderLibrarySelector`) are
already label-left (they pair a combo with a manual entry field, so they keep their own
two-control row rather than `beginPropertyRow`). When adding a control to any inspector
panel, wrap it in `beginPropertyRow`/`endPropertyRow` with a `##` hidden control label —
the file should contain no `ImGui::Widget("Visible Label", …)` calls.
