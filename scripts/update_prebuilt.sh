#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

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

target_name="${1:-vasset-cli}"
platform="${2:-$(normalize_platform)}"
arch="${3:-$(normalize_arch)}"
mode="${4:-release}"

bin_name="$target_name"
if [ "$platform" = "windows" ]; then
    bin_name="$target_name.exe"
fi

source_bin="$repo_root/build/$platform/$arch/$mode/$target_name/$bin_name"
dest_dir="$repo_root/prebuilt/$platform/$arch"
dest_raw="$dest_dir/$target_name-raw"

if [ "$platform" = "windows" ]; then
    dest_raw="$dest_dir/$target_name-raw.exe"
fi

echo "Configuring xmake for $platform/$arch ..."
xmake f -p "$platform" -a "$arch" -m "$mode"

echo "Building $target_name ($mode) ..."
xmake build "$target_name"

if [ ! -f "$source_bin" ]; then
    echo "Built binary not found: $source_bin" >&2
    exit 1
fi

mkdir -p "$dest_dir"
cp "$source_bin" "$dest_raw"

echo "Updated prebuilt binary:"
echo "  source: $source_bin"
echo "  dest:   $dest_raw"
