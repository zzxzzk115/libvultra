#!/usr/bin/env sh
set -eu

repo_root_input="${1:-$(dirname "$0")/..}"
repo_root="$(cd "$repo_root_input" && pwd -P)"

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
vasset_project_dir="$repo_root/external/vasset"

if [ ! -d "$vasset_project_dir" ]; then
    echo "vasset project not found: $vasset_project_dir" >&2
    exit 1
fi

xmake f -P "$vasset_project_dir" -p "$platform" -a "$arch" -m release --vasset_build_examples=n --vasset_build_tests=n -y
xmake build -P "$vasset_project_dir" -y vasset-cli
xmake install -P "$vasset_project_dir" -y -o "$install_root" vasset-cli

echo "Installed host vasset-cli to: $install_root/bin/vasset-cli"
