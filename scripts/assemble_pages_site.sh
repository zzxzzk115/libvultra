#!/usr/bin/env bash
# Assemble the GitHub Pages site: the landing page, ONE shared web export under
# example/ (runtime shell + vultra-runtime.js/.wasm + resources.vpk), and a tiny
# entry stub per scene that redirects back to the shared shell with ?scene=.
# The stub must land on example/index.html: the shell fetches the vpk relative
# to its own URL, so the shared resources.vpk resolves only from that page.
#
# Usage: assemble_pages_site.sh [export-dir] [out-dir]
#   export-dir  output of `vultra --export --export-platform web` (default: export-web)
#   out-dir     site root to (re)create (default: site)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
EXPORT_DIR="${1:-$ROOT/export-web}"
OUT="${2:-$ROOT/site}"

SHA="${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo dev)}"
SHA="${SHA:0:7}"

for f in index.html vultra-runtime.js vultra-runtime.wasm resources.vpk; do
    if [ ! -f "$EXPORT_DIR/$f" ]; then
        echo "assemble_pages_site: missing $EXPORT_DIR/$f (run the web export first)" >&2
        exit 1
    fi
done

rm -rf "$OUT"
mkdir -p "$OUT/example"

cp "$ROOT/web/index.html" "$OUT/index.html"
touch "$OUT/.nojekyll"

cp "$EXPORT_DIR/index.html" \
   "$EXPORT_DIR/vultra-runtime.js" \
   "$EXPORT_DIR/vultra-runtime.wasm" \
   "$EXPORT_DIR/resources.vpk" \
   "$OUT/example/"

# Cache-bust the runtime script (Pages serves with max-age=600). The .wasm URL
# is derived inside emscripten's locateFile -- leave it alone.
sed -i "s/vultra-runtime\.js/vultra-runtime.js?v=${SHA}/g" "$OUT/example/index.html"

# $2 is the scene URI already URL-encoded for use as a query-string value.
make_stub() { # slug encoded-scene-uri title
    local slug="$1" scene="$2" title="$3"
    local q="scene=${scene}&vpk=resources.vpk%3Fv%3D${SHA}"
    mkdir -p "$OUT/example/$slug"
    cat > "$OUT/example/$slug/index.html" <<EOF
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>${title} | VultraEngine</title>
  <meta http-equiv="refresh" content="0; url=../index.html?${q}">
  <script>location.replace("../index.html?${q}");</script>
</head>
<body>
  <p><a href="../index.html?${q}">Launch ${title}</a></p>
</body>
</html>
EOF
}

make_stub sponza "res%3A%2F%2Fscenes%2Fsponza.vscn"       "Sponza"
make_stub 3dgs   "res%3A%2F%2Fscenes%2F3dgs_example.vscn" "3D Gaussian Splatting"

echo "site assembled at $OUT (build $SHA)"
du -sh "$OUT" "$OUT/example/resources.vpk"
