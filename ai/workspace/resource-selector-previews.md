# Resource Selector Previews

## Goal

Texture and mesh selector fields should show a thumbnail for the currently
selected resource, not only the resource name. The popup grid already shows
thumbnails, but closed fields also need visual context.

## Changes

- Texture URI/UUID fields now render a compact preview row with thumbnail,
  display name, and URI.
- Mesh UUID fields now render a compact preview row with thumbnail, display
  name, and resolved URI/imported path.
- Preview rows reuse `AssetThumbnailService` and `AssetPreviewCache`.
- Material graph Texture2D node parameters now use the same texture preview row.
- MeshComponent `mesh` fields now use the mesh selector preview row instead of
  the generic UUID text field.
- Preview field text is clipped and shortened with ellipses so long filenames
  and URIs stay inside material node and Inspector bounds.
- Mesh material overrides use a material-slot combo derived from the selected
  mesh's imported material names instead of exposing only a raw slot integer.

## Verification

- Current: `xmake build -y vultra-app` passed.
