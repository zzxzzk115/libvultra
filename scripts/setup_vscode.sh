#!/bin/sh
set -eu

if [ "$#" -gt 1 ]; then
    echo 'Usage: setup_vscode.sh [project-directory]' >&2
    exit 1
fi
if ! command -v xmake >/dev/null 2>&1; then
    echo 'xmake must be available on PATH.' >&2
    exit 1
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=${1:-"$script_dir/.."}
exec xmake lua "$script_dir/setup_vscode.lua" "$project_dir"
