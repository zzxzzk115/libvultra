# Task: Runtime i18n -> Lua + UiText localization (cross-cutting must-haves, Phase 3)

Spec: `ai/specs/runtime-i18n-lua.md`.

## In scope
- `I18n` Lua shim (tr/setLanguage/language/languages/displayName/setPseudolocalize).
- `UiTextComponent.localizationKey` + render resolution via ambient `tr()` + reflection.

## Out of scope (follow-ons)
- `I18n.trf` varargs; locale font fallback; Lua language-changed callbacks; editor key picker.

## Implementation plan (done)
1. `script_i18n_shim.{hpp,cpp}` (`I18n` module, area `i18n`); `script_i18n_binding.hpp` +
   aggregator call in `script_binding.cpp`; manifest entry.
2. `script_context.hpp` `i18nService` + populated in `script_system.cpp` (+ include).
3. `ui_components.hpp` `UiTextComponent::localizationKey`; `render_system.cpp` resolves via
   `vultra::tr()`; `scene_reflection.cpp` reflects it.
4. Codegen -> `script_i18n_binding.gen.cpp` + stub.
5. Docs: `doc/lua_scripting.md` + CN "Localization (I18n)".

## Verification
- `xmake build -y vultra-app`; `xmake run test-lua-api-conformance` PASS.
- `I18n.setLanguage` switches a `tr` string + a `UiText.localizationKey` label.

## Status
Code + bindings + docs implemented; pending local build/conformance verification.
