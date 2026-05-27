# Editor Settings Persistence

## Goal

Persist editor-local settings, including theme selection and custom theme colors,
under `.vultra/` so they survive editor restarts without mixing into project
content or tracked engine knowledge.

## Decisions

- Use `.vultra/editor_settings.json` next to existing `.vultra/launcher_projects.txt`.
- Keep `.vultra/` local runtime/editor state; do not use it for AI memory.
- Load settings during `VultraStandaloneApp` construction before editor UI draws.
- Save settings from the Editor Settings modal Save button.

## Verification

- Passed: `xmake build -y vultra-app`

## Follow-up

- Fixed playback toolbar colors so Light and Custom themes do not inherit
  hard-coded dark-theme icon, border, and active-hover colors.
- Passed: `xmake build -y vultra-app`
