# Content Browser Asset Creation Task

## Goal

Support creating registered project assets from the Content Browser, starting
with scene and Lua script assets.

## In Scope

- Add an editor-side content asset creator registry.
- Add built-in creators for `.vscn` scenes and `.lua` scripts.
- Add `Create` menu support in empty-folder and asset context menus.
- Reimport created files so the asset registry can resolve them.
- Record the Lua/plugin extension direction in a spec.

## Out of Scope

- Loading Lua editor plugins.
- Plugin package discovery.
- Non-text asset generation.
- Scene/template selection UI.

## Relevant Spec

- `ai/specs/editor-content-asset-creation.md`

## Verification

- `xmake build -y vultra-app` passed.

## Handoff

Next slice can add an editor extension service that loads trusted Lua plugin
files and maps `editor.assets.register_creator` calls into
`ContentAssetRegistry`.
