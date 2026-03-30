#!/usr/bin/env sh
set -eu

repo_root="$1"
asset_root="$2"

normalize_platform() {
    case "$(uname -s)" in
        Darwin*) echo "macos" ;;
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
vasset_name="vasset-cli"
if [ "$platform" = "windows" ]; then
    vasset_name="vasset-cli.exe"
fi

vasset_cli="$repo_root/prebuilt/$platform/$arch/$vasset_name"
if [ ! -f "$vasset_cli" ]; then
    echo "Missing prebuilt vasset-cli: $vasset_cli" >&2
    exit 1
fi

exec "$vasset_cli" import "$asset_root"
