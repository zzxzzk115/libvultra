# Spec: Runtime i18n exposed to Lua + UiText localization

Status: implemented (cross-cutting must-haves, Phase 3).

## Intent

Let games localize content at runtime. The i18n system already powers the editor; this exposes
it to gameplay scripts and to authored UI text, so a shipped game can switch language and show
translated strings.

## Public interface

- Lua `I18n` namespace: `tr(key)`, `setLanguage(locale)`, `language()`, `languages()`,
  `displayName(locale)`, `setPseudolocalize(enabled)`. (No varargs format in v1 — use
  `string.format(I18n.tr(key), ...)`.)
- `UiTextComponent.localizationKey`: when non-empty, the text render path shows
  `tr(localizationKey)` for the active language instead of the literal `text` — editor-authored
  UI localizes with no script.

## Design / constraints

- `I18n` is a **shim** module (`script_i18n_shim`, area `i18n`) like `Nav`/`Save`, not a
  serviceForward on `II18nService` — its `const char*` / `std::vector<std::string>` returns
  aren't serviceForward-friendly. `ScriptContext` gains `i18nService` (populated in
  `ScriptSystem::onInit`); `II18nService` itself stays Lua-agnostic.
- UiText render (`render_system.cpp`) resolves the display string via the ambient `vultra::tr()`
  (active translator) when `localizationKey` is set. `localizationKey` is reflected
  (`scene_reflection.cpp`) for serialization + inspector.

## Out of scope (follow-ons)

- Varargs `I18n.trf`; per-locale font fallback selection; live `onLanguageChanged` callbacks in
  Lua; editor catalog-key picker for `localizationKey`.

## Acceptance

- Lua switches language + reads `tr`; a `UiText.localizationKey` updates with language;
  conformance PASS; docs in `doc/lua_scripting.md` (EN+CN).
