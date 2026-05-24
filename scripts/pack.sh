#!/usr/bin/env sh
set -eu

repo_root="$(cd "$1" && pwd -P)"
asset_root="$2"
out_vpk="$3"
shift 3
if [ "${1:-}" = "--no-bootstrap" ]; then
    shift
fi

case "$asset_root" in
    /*) ;;
    *) asset_root="$repo_root/$asset_root" ;;
esac

case "$out_vpk" in
    /*) ;;
    *) out_vpk="$repo_root/$out_vpk" ;;
esac

find_vultra() {
    if [ -n "${VULTRA:-}" ] && [ -x "$VULTRA" ]; then
        printf '%s\n' "$VULTRA"
        return 0
    fi

    name="vultra"
    for candidate in \
        "$repo_root/build/install/bin/$name" \
        "$repo_root/build/install/vultra-app/bin/$name" \
        "$repo_root/build/linux/x64/release/vultra-app/$name" \
        "$repo_root/build/linux/x86_64/release/vultra-app/$name" \
        "$repo_root/build/macosx/arm64/release/vultra-app/$name"
    do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    echo "vultra executable not found. Build vultra-app first, or set VULTRA to the executable path." >&2
    return 1
}

vultra="$(find_vultra)"
"$vultra" asset import "$asset_root" --reimport
exec "$vultra" asset pack "$asset_root" "$out_vpk" --zstd 6 "$@"
