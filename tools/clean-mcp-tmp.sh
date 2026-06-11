#!/usr/bin/env bash
# Reclaim Runtime MCP smoke-test scratch under build/.tmp.
#
# Runtime MCP smoke tests create a fresh timestamped scratch directory on every run
# (build/.tmp/material-mcp-smoke-*, material-mcp-step-*, material-mcp-smoke-output-*,
# material-shader-frame-textures, ...). build/ is gitignored so these are never
# committed, but they accumulate unbounded on local disk. This script removes that
# scratch. It only ever touches build/.tmp, never tracked content.
#
# Usage:
#   tools/clean-mcp-tmp.sh           # remove known smoke scratch prefixes
#   tools/clean-mcp-tmp.sh --dry-run # list what would be removed
#   tools/clean-mcp-tmp.sh --all     # remove everything under build/.tmp
set -euo pipefail

dry_run=0
all=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) dry_run=1 ;;
        --all)     all=1 ;;
        -h|--help)
            sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

# Repo root = parent of this script's tools/ directory.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
tmp_root="$repo_root/build/.tmp"

if [[ ! -d "$tmp_root" ]]; then
    echo "Nothing to clean: $tmp_root does not exist."
    exit 0
fi

# Known MCP smoke scratch prefixes. Extend here if new smoke flows add prefixes.
prefixes=(material-mcp-smoke material-mcp-step material-mcp-smoke-output material-shader-frame-textures streamline-diagnostics-smoke mcp_projects)

shopt -s nullglob
targets=()
if [[ "$all" -eq 1 ]]; then
    for entry in "$tmp_root"/*; do
        targets+=("$entry")
    done
else
    for prefix in "${prefixes[@]}"; do
        for entry in "$tmp_root/$prefix"*; do
            targets+=("$entry")
        done
    done
fi

if [[ "${#targets[@]}" -eq 0 ]]; then
    echo "Nothing to clean under $tmp_root."
    exit 0
fi

for t in "${targets[@]}"; do
    size="$(du -sh "$t" 2>/dev/null | cut -f1)"
    if [[ "$dry_run" -eq 1 ]]; then
        echo "would remove $(basename "$t") ($size)"
    else
        echo "removing $(basename "$t") ($size)"
        rm -rf "$t"
    fi
done

if [[ "$dry_run" -eq 1 ]]; then
    echo "Dry run complete; nothing was deleted."
else
    echo "Cleaned MCP smoke scratch under $tmp_root."
fi
