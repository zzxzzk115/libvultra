# Task: UI widgets (input field, dropdown, scroll view)

Spec: `ai/specs/ui-widgets.md`. Delivered as three sub-commits on `dev-next`.

## Shared infra
- [x] `function/services/ui_service.hpp`: `UiEventType` += `ValueChanged`, `Submit`.
- [x] `function/ui/ui_system.hpp`: `entt::entity m_FocusedEntity {entt::null}`.
- [x] CPU-side clipping (cull/clamp at render emission) — no UI-shader change.

## a) UiInputField (commit 2a)
- [x] `ui_components.hpp`: `UiInputFieldComponent`.
- [x] `ui_system.cpp`: focus + keyboard editing in `updateInput`; reset focus on shutdown;
      reset `submitted` in `onPostUpdate`.
- [x] `render_system.cpp`: `pushGlyphRect` lambda + input-field render block (bg, placeholder/
      text with horizontal scroll + overflow cull, caret quad).
- [x] Bindings: `script_types.hpp` ref, `script_entity_shim.{hpp,cpp}` enum + X-macro,
      `script_ui_shim.cpp` usertype + signals; regenerated `.gen.cpp` + stub.
- [x] `scene_reflection.cpp`: meta factory (authorable fields only).
- [x] `inspector_window.cpp`: 6 registration sites under category "UI".
- [x] i18n: `inspector.component.uiInputField` in en/zh-CN/ja/ko.
- [x] Docs: EN + CN UI section.

## b) UiDropdown (commit 2b)
- [x] `ui_system.cpp`: header toggle, in-component option hit-test, selection, camera suppress.
- [x] `render_system.cpp`: header + overlay list (overlayBias sort) with selected highlight.
- [x] Bindings (script_types ref, enum+X-macro, UiDropdown usertype + onValueChanged signal).
- [x] `scene_reflection.cpp` meta factory + `std::vector<std::string>` scene (de)serialization.
- [x] Inspector: custom options editor + 6 registration sites (UI category).
- [x] i18n: `inspector.component.uiDropdown` + `inspector.uiDropdown.{options,addOption}` x4.
- [x] Docs: EN + CN UI section.

## c) UiScrollView (commit 2c)
- [x] `ui_system.cpp`: wheel scroll (innermost view), clamp, child rect offset in `rebuild`.
- [x] `render_system.cpp`: generic CPU clip (rect+UV clamp) threaded through cookUiChildren;
      scroll background + child offset + clip-to-bounds.
- [x] Bindings (ref, enum+X-macro, UiScrollView usertype: scrollPx/contentSizePx/h/v).
- [x] `scene_reflection.cpp` meta factory.
- [x] Inspector: generic drawMetaFields + 6 registration sites (UI category).
- [x] i18n: `inspector.component.uiScrollView` x4.
- [x] Docs: EN + CN UI section.

## Verify
- `xmake build -y vultra-app` (close running editor to relink).
- `xmake run test-lua-api-conformance` PASS.
