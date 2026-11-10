#!/usr/bin/env python3
"""Cook the optional explicit-fragment-depth transparency diagnostic."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shader-tool", required=True, type=Path, help="Host-built vultra-shader executable")
    parser.add_argument("--output", required=True, type=Path, help="AssetSource root to populate with cooked passes")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve() / "passes"
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(args.shader_tool.resolve(strict=True)),
                    str(root / "builtin/shaders/passes/transparent.slang"),
                    "--include", str(root / "builtin/shaders"), "--include", str(root / "external"),
                    "--define", "VULTRA_TRANSPARENCY_EXPLICIT_DEPTH=1", "--output",
                    str(output / "transparent_explicit_depth.vshaderc")], check=True)


if __name__ == "__main__":
    main()
