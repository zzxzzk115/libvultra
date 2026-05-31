# Export Output Directory Fix

Date: 2026-05-31

## Context

IGFD reported a missing directory such as:

`C:/Users/Administrator/GitHub/libvultra/build/test/build`

The Export & Run flow was auto-populating an output directory with
`<project root>/build`. That path may not exist, and it also made the UI look as
if an output folder had been selected when the user had not chosen one.

## Change

- Removed the implicit `<project root>/build` default from Export & Run and
  Export Settings.
- Added a `FileDialogField` default browse path that affects only the dialog
  starting directory, not the saved field value.
- Set Export output directory browse dialogs to start at the current project
  root when the field is empty.
- Made file dialog startup paths fall back to the nearest existing directory so
  IGFD does not receive a nonexistent path.

## Verification

- `git diff --check`
- `xmake build -y vultra-app`
