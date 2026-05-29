# Asset Registry Reconcile

## Context

Manual file deletion can leave `resources/imported/asset_registry.tsv` with UUID
entries whose source asset or imported payload no longer exists.

## Change

- `vasset::VAssetRegistry::cleanup()` already removes entries with missing
  source or imported files.
- `AssetSystem::configure()` now calls `cleanup()` immediately after loading a
  physical asset registry.
- If cleanup removes entries, the reconciled registry is saved back to disk and
  a log message reports the stale-entry count.
- VPK mode is unchanged because the registry comes from the mounted package.

## Verification

- `git diff --check -- source/vultra/src/function/asset/asset_system.cpp`
- `xmake build -y vultra-app`
