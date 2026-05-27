# vasset-example-vpk C++ Import

## Change

- Replaced the `vasset-example-vpk` build hook that executed `vasset-cli import`
  and `vasset-cli pack`.
- Added a small libvasset C++ packing API so examples can import and pack assets
  without shelling out to the CLI.
- Updated `vasset-example-vpk` to import only the assets it validates, pack them
  into `out.vpk`, and then run the existing VPK read/load checks.

## Verification

- `xmake build -y vasset-example-vpk` completed successfully.
- The build-produced example imported DamagedHelmet plus `awesomeface.png`, packed
  an 8-entry VPK, opened it, loaded the mesh, resolved the material texture, and
  loaded the texture.

## Handoff

- The `vasset-cli` target still exists as a standalone tool, but
  `vasset-example-vpk` no longer depends on it or invokes it.
