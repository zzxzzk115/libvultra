#!/usr/bin/env python3
"""Cook clustered lighting and experimental compact G-buffer variants."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--shader-tool", required=True, type=Path, help="Host-built vultra-shader executable"
    )
    parser.add_argument(
        "--output", required=True, type=Path, help="AssetSource root to populate with cooked passes"
    )
    parser.add_argument(
        "--visibility-color-input-only",
        action="store_true",
        help="Cook the three Vulkan input-attachment visibility variants",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    shader_root = root / "builtin/shaders"
    tool = args.shader_tool.resolve(strict=True)
    output = args.output.resolve() / "passes"
    output.mkdir(parents=True, exist_ok=True)
    scene_lights = (
        "--define",
        "VULTRA_OPENPBR_UNCOATED_SURFACE=1",
        "--define",
        "VULTRA_SURFACE_IBL=0",
        "--define",
        "VULTRA_SURFACE_PRIMARY_LIGHT=0",
        "--define",
        "VULTRA_SURFACE_DEBUG=0",
    )
    sampled_depth = ("--define", "VULTRA_TRANSPARENCY_SAMPLED_DEPTH=1")
    early_depth = ("--define", "VULTRA_TRANSPARENCY_EARLY_DEPTH=1")
    opaque = ("--define", "VULTRA_OPENPBR_OPAQUE_SURFACE=1")
    uncoated = ("--define", "VULTRA_OPENPBR_UNCOATED_SURFACE=1")
    point_lights = (*scene_lights, "--define", "VULTRA_POINT_LIGHTS_ONLY=1")
    point_transparency = (
        *opaque,
        *point_lights,
        "--define",
        "VULTRA_TRANSPARENCY_SKIP_ZERO_WEIGHT_MAPS=1",
    )
    variants = (
        ("visibility_lighting", "visibility_lighting_point_lights", point_lights),
        (
            "transparent",
            "transparent_sampled_depth_point_lights",
            (*sampled_depth, *point_transparency),
        ),
        ("transparent", "transparent_sampled_depth", sampled_depth),
        (
            "transparent",
            "transparent_sampled_depth_uncoated",
            (
                *sampled_depth,
                "--define",
                "VULTRA_OPENPBR_OPAQUE_SURFACE=1",
                "--define",
                "VULTRA_OPENPBR_UNCOATED_SURFACE=1",
            ),
        ),
        (
            "transparent",
            "transparent_sampled_depth_scene_lights",
            (*sampled_depth, *scene_lights, "--define", "VULTRA_OPENPBR_OPAQUE_SURFACE=1"),
        ),
        ("transparent", "transparent_early_depth", early_depth),
        ("transparent", "transparent_early_depth_uncoated", (*early_depth, *opaque, *uncoated)),
        ("transparent", "transparent_early_depth_scene_lights", (*early_depth, *opaque, *scene_lights)),
        ("transparent", "transparent_early_depth_point_lights", (*early_depth, *point_transparency)),
        ("clustered_lights", "clustered_lights", ("--entry", "compute:computeMain")),
        (
            "visibility_lighting",
            "visibility_lighting_clustered",
            ("--define", "VULTRA_CLUSTERED_LIGHTING=1"),
        ),
        (
            "visibility_lighting",
            "visibility_lighting_uncoated",
            ("--define", "VULTRA_OPENPBR_UNCOATED_SURFACE=1"),
        ),
        (
            "visibility_lighting",
            "visibility_lighting_clustered_uncoated",
            (
                "--define",
                "VULTRA_CLUSTERED_LIGHTING=1",
                "--define",
                "VULTRA_OPENPBR_UNCOATED_SURFACE=1",
            ),
        ),
        ("visibility_lighting", "visibility_lighting_scene_lights", scene_lights),
        (
            "visibility_lighting",
            "visibility_lighting_clustered_scene_lights",
            (*scene_lights, "--define", "VULTRA_CLUSTERED_LIGHTING=1"),
        ),
        (
            "deferred_lighting",
            "deferred_lighting_clustered",
            ("--define", "VULTRA_CLUSTERED_LIGHTING=1"),
        ),
        ("gbuffer", "gbuffer_relative16", ("--define", "VULTRA_GBUFFER_CAMERA_RELATIVE_16=1")),
        (
            "deferred_lighting",
            "deferred_lighting_relative16",
            ("--define", "VULTRA_GBUFFER_CAMERA_RELATIVE_16=1"),
        ),
        (
            "deferred_lighting",
            "deferred_lighting_clustered_relative16",
            (
                "--define",
                "VULTRA_CLUSTERED_LIGHTING=1",
                "--define",
                "VULTRA_GBUFFER_CAMERA_RELATIVE_16=1",
            ),
        ),
        ("gbuffer", "gbuffer_depth32", ("--define", "VULTRA_GBUFFER_DEPTH_32=1")),
        (
            "deferred_lighting",
            "deferred_lighting_depth32",
            ("--define", "VULTRA_GBUFFER_DEPTH_32=1"),
        ),
        (
            "deferred_lighting",
            "deferred_lighting_clustered_depth32",
            ("--define", "VULTRA_CLUSTERED_LIGHTING=1", "--define", "VULTRA_GBUFFER_DEPTH_32=1"),
        ),
        (
            "deferred_lighting",
            "deferred_lighting_opaque_specialized",
            ("--define", "VULTRA_OPENPBR_OPAQUE_SURFACE=1"),
        ),
        (
            "deferred_lighting",
            "deferred_lighting_reference",
            ("--define", "VULTRA_OPENPBR_REFERENCE=1"),
        ),
        (
            "visibility_lighting",
            "visibility_lighting_opaque_specialized",
            ("--define", "VULTRA_OPENPBR_OPAQUE_SURFACE=1"),
        ),
        (
            "visibility_lighting",
            "visibility_lighting_reference",
            ("--define", "VULTRA_OPENPBR_REFERENCE=1"),
        ),
        (
            "transparent",
            "transparent_opaque_specialized",
            ("--define", "VULTRA_OPENPBR_OPAQUE_SURFACE=1"),
        ),
        (
            "transparent",
            "transparent_uncoated",
            (
                "--define",
                "VULTRA_OPENPBR_OPAQUE_SURFACE=1",
                "--define",
                "VULTRA_OPENPBR_UNCOATED_SURFACE=1",
            ),
        ),
        (
            "transparent",
            "transparent_explicit_depth_uncoated",
            (
                "--define",
                "VULTRA_OPENPBR_OPAQUE_SURFACE=1",
                "--define",
                "VULTRA_OPENPBR_UNCOATED_SURFACE=1",
                "--define",
                "VULTRA_TRANSPARENCY_EXPLICIT_DEPTH=1",
            ),
        ),
        (
            "transparent",
            "transparent_scene_lights",
            (*scene_lights, "--define", "VULTRA_OPENPBR_OPAQUE_SURFACE=1"),
        ),
        (
            "transparent",
            "transparent_explicit_depth_scene_lights",
            (
                *scene_lights,
                "--define",
                "VULTRA_OPENPBR_OPAQUE_SURFACE=1",
                "--define",
                "VULTRA_TRANSPARENCY_EXPLICIT_DEPTH=1",
            ),
        ),
        (
            "transparent",
            "transparent_explicit_depth_opaque_specialized",
            (
                "--define",
                "VULTRA_OPENPBR_OPAQUE_SURFACE=1",
                "--define",
                "VULTRA_TRANSPARENCY_EXPLICIT_DEPTH=1",
            ),
        ),
    )
    if args.visibility_color_input_only:
        local = (
            "--entry",
            "vertex:screenVertex",
            "--entry",
            "fragment:fragmentMain",
            "--entry",
            "fragment:fragmentBatchedMain",
            "--define",
            "VULTRA_VISIBILITY_COLOR_INPUT=1",
        )
        variants = (
            ("visibility_lighting", "visibility_color_input", local),
            (
                "visibility_lighting",
                "visibility_color_input_uncoated",
                (*local, "--define", "VULTRA_OPENPBR_UNCOATED_SURFACE=1"),
            ),
            ("visibility_lighting", "visibility_color_input_scene_lights", (*local, *scene_lights)),
        )
    # Do not replace existing assets if one of the variants fails to compile.
    with tempfile.TemporaryDirectory(prefix="vultra-clustered-shaders-", dir=output) as folder:
        staging = Path(folder)
        for source, destination, options in variants:
            subprocess.run(
                [
                    str(tool),
                    str(shader_root / "passes" / (source + ".slang")),
                    "--include",
                    str(shader_root),
                    "--include",
                    str(root / "external"),
                    *options,
                    "--output",
                    str(staging / (destination + ".vshaderc")),
                ],
                check=True,
            )
        for _, destination, _ in variants:
            name = destination + ".vshaderc"
            (staging / name).replace(output / name)


if __name__ == "__main__":
    main()
