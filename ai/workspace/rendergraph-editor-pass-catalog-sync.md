# RenderGraph Editor Pass Catalog Sync

## Summary

- Fixed the RenderGraph editor catalog missing `GeneralGaussianSplatComposite`.
- Runtime declarative renderer already registered this builtin pass, so graphs rendered correctly but editor validation reported:
  - `Unknown pass type 'GeneralGaussianSplatComposite' on pass 'GeneralGaussianSplatComposite'.`

## Verification

- Scanned builtin/project `.vrg.json` pass types under `builtin/render` and `resources/render`.
- Compared runtime builtin pass registrations with editor builtin pass registrations for the Gaussian path.
- `xmake build -y vultra-app`
  - Passed.
