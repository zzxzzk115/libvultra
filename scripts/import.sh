#!/usr/bin/env sh
set -eu

repo_root="$(cd "$1" && pwd -P)"
asset_root="$2"

case "$asset_root" in
    /*) ;;
    *) asset_root="$repo_root/$asset_root" ;;
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

(cd "$repo_root" && \
    xmake f -p "$platform" -a "$arch" -m release -y && \
    xmake install -y -o "$install_root" vasset-cli)

vasset_cli="$install_root/bin/$vasset_name"
if [ ! -f "$vasset_cli" ]; then
    echo "Installed vasset-cli not found: $vasset_cli" >&2
    exit 1
fi

if [ -d "$install_root/lib" ]; then
    export LD_LIBRARY_PATH="$install_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export DYLD_LIBRARY_PATH="$install_root/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
fi
export PATH="$install_root/bin:$PATH"

exec "$vasset_cli" import "$asset_root"
