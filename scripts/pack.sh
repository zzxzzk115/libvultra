#!/usr/bin/env sh
set -eu

repo_root="$(cd "$1" && pwd -P)"
asset_root="$2"
out_vpk="$3"
shift 3
no_bootstrap=0
if [ "${1:-}" = "--no-bootstrap" ]; then
    no_bootstrap=1
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

normalize_platform() {
    case "$(uname -s)" in
        Darwin*) echo "macosx" ;;
        Linux*) echo "linux" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *) echo "$(uname -s | tr '[:upper:]' '[:lower:]')" ;;
    esac
}

normalize_arch() {
    case "$(uname -m)" in
        x86_64|amd64) echo "x64" ;;
        arm64|aarch64) echo "arm64" ;;
        *) echo "$(uname -m)" ;;
    esac
}

platform="$(normalize_platform)"
arch="$(normalize_arch)"

install_root="$repo_root/build/.generated/vasset-host/$platform/$arch/release"
vasset_name="vasset-cli"

if [ "$no_bootstrap" -eq 0 ]; then
    sh "$repo_root/scripts/bootstrap_vasset_cli.sh" "$repo_root"
fi

vasset_cli="$install_root/bin/$vasset_name"
if [ ! -f "$vasset_cli" ]; then
    if [ "$no_bootstrap" -eq 1 ]; then
        echo "Installed vasset-cli not found: $vasset_cli" >&2
        echo "Bootstrap once outside xmake:" >&2
        echo "  sh \"$repo_root/scripts/bootstrap_vasset_cli.sh\" \"$repo_root\"" >&2
        exit 1
    fi
    echo "Installed vasset-cli not found: $vasset_cli" >&2
    exit 1
fi

if [ -d "$install_root/lib" ]; then
    export LD_LIBRARY_PATH="$install_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export DYLD_LIBRARY_PATH="$install_root/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
fi
export PATH="$install_root/bin:$PATH"

exec "$vasset_cli" pack "$asset_root" "$out_vpk" --zstd 6 "$@"
