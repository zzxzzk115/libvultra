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
- [ ] `ui_system.cpp`: header toggle, in-component option hit-test, selection.
- [ ] `render_system.cpp`: header + overlay list with hovered/selected highlight.
- [ ] Bindings + reflection + inspector + i18n + docs.

## c) UiScrollView (commit 2c)
- [ ] `ui_system.cpp`: wheel/touch scroll, clamp, child rect offset in `rebuild`.
- [ ] `render_system.cpp`: child clip-clamp to bounds.
- [ ] Bindings + reflection + inspector + i18n + docs.

## Verify
- `xmake build -y vultra-app` (close running editor to relink).
- `xmake run test-lua-api-conformance` PASS.
