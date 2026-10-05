"""Run the standalone offline renderer without a display server or build tools."""

import argparse
import array
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    root = Path(__file__).resolve().parents[1]
    temporary = root / "build/.tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="offline-qa-", dir=temporary))
    environment = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "VULTRA_WINDOW_SYSTEM"):
        environment.pop(name, None)
    environment["PATH"] = str(run / "no-build-tools")

    def invoke(name, options, expected=0, executable=binary, cwd=root):
        result = subprocess.run(
            [str(executable), *options], cwd=cwd, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90,
        )
        (run / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        assert result.returncode == expected, f"{name}: {result.stdout}"
        if expected == 0:
            assert "[error]" not in result.stdout and "[warning]" not in result.stdout, result.stdout
        print(f"{name}: passed", flush=True)
        return result.stdout

    invoke("help", ["--help"])
    description = root / "resources/research.vexperiment"
    invoke("experiment-conflicting-flags", [
        "--experiment", str(description), "--width", "128", "--revision", "qa-working-tree",
        "--output", str(run / "conflict"),
    ], expected=1)
    invoke("experiment-bad-version", [
        "--experiment", str(run / "missing.vexperiment"), "--revision", "qa-working-tree",
        "--output", str(run / "missing-description"),
    ], expected=1)

    common = ["--width", "129", "--height", "97", "--frames", "2", "--warmup", "1", "--revision", "qa-working-tree"]
    model = ["--model", str(root / "resources/models/DamagedHelmet/DamagedHelmet.glb"),
             "--environment", str(root / "resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr")]
    definition = ["--graph", str(root / "examples/research/color_gain.vgraph")]
    invoke("baseline", common + model + ["--output", str(run / "baseline")])
    invoke("cpp", common + model + ["--color-gain", "0.5", "--output", str(run / "cpp")])
    invoke("definition", common + model + definition + [
        "--output", str(run / "definition"), "--compare", str(run / "cpp/final.png"),
    ])
    assert (run / "cpp/final.png").read_bytes() == (run / "definition/final.png").read_bytes()
    assert (run / "cpp/scene_hdr.pfm").read_bytes() == (run / "definition/scene_hdr.pfm").read_bytes()
    assert (run / "baseline/final.png").read_bytes() != (run / "cpp/final.png").read_bytes()
    manifest = json.loads((run / "definition/report/manifest.json").read_text())
    assert manifest["version"] == 1 and manifest["window_system"] == "offscreen"
    assert manifest["present_mode"] == "none" and manifest["measured_frames"] == 2
    assert manifest["parameters"]["mse"] == "0" and float(manifest["parameters"]["ssim"]) == 1
    assert manifest["parameters"]["output_000.pfm"] == "gain.color"
    with (run / "cpp/scene_hdr.pfm").open("rb") as stream:
        assert stream.readline() == b"PF\n" and stream.readline() == b"129 97\n"
        little_endian = float(stream.readline()) < 0
        pixels = array.array("f")
        pixels.frombytes(stream.read())
        if little_endian != (sys.byteorder == "little"):
            pixels.byteswap()
    assert len(pixels) == 129 * 97 * 3 and all(math.isfinite(value) for value in pixels)
    assert max(pixels) > 1, "HDR output was clipped or tone mapped"
    zero_frames = common.copy()
    zero_frames[zero_frames.index("--frames") + 1] = "0"
    zero = invoke("zero-frames", zero_frames + model + ["--output", str(run / "zero")], 1)
    assert "requires nonzero" in zero
    assert "new offline output" in invoke("overwrite", common + model + ["--output", str(run / "cpp")], 1)
    bad = json.loads((root / "examples/research/color_gain.vgraph").read_text())
    bad["edges"][0]["from"] = "scene.missing"
    bad_file = run / "invalid.vgraph"
    bad_file.write_text(json.dumps(bad), encoding="utf-8")
    invalid = invoke("invalid-graph", common + model + [
        "--graph", str(bad_file), "--output", str(run / "invalid"),
    ], 1)
    assert "scene.missing" in invalid
    assert not (run / "invalid").exists()
    invoke("recovery", common + model + definition + ["--output", str(run / "recovered")])
    assert (run / "cpp/final.png").read_bytes() == (run / "recovered/final.png").read_bytes()

    scripted = run / "scripted"
    scripted.mkdir()
    scene = json.loads((root / "resources/scenes/research.vscene").read_text())
    scene["root"]["children"][0]["model"] = "11111111-1111-4111-8111-111111111111"
    (scripted / "scene.vscene").write_text(json.dumps(scene), encoding="utf-8")
    (scripted / "quad.obj").write_text(
        "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n", encoding="utf-8",
    )
    logic = """local Motion = {}
function Motion:_ready()
    self.time = 0
    self.mesh = self.node:get_child(0)
end
function Motion:_process(delta)
    self.time = self.time + delta
    self.mesh:set_position(self.time, 0, 0)
end
function Motion:_editor_gui()
    error("Offline experiments must not call the GUI phase")
end
return Motion
"""
    (scripted / "logic.lua").write_text(logic, encoding="utf-8")
    script_project = {
        "format": "vultra.project", "version": 1, "main_scene": "scene.vscene",
        "assets": [{"id": "11111111-1111-4111-8111-111111111111", "path": "quad.obj"}],
        "extensions": [], "scripts": [{"language": "lua", "path": "logic.lua"}],
    }
    script_file = scripted / "project.vproject"
    script_file.write_text(json.dumps(script_project), encoding="utf-8")
    script_args = common + ["--project", str(script_file), "--time-step", "0.25"]
    invoke("scripted-project", script_args + ["--output", str(run / "scripted-output")])
    invoke("scripted-repeat", script_args + ["--output", str(run / "scripted-repeat")])
    for filename in ("final.png", "scene_hdr.pfm", "final.vscene"):
        assert (run / "scripted-output" / filename).read_bytes() == (run / "scripted-repeat" / filename).read_bytes()
    snapshot = json.loads((run / "scripted-output/final.vscene").read_text())
    assert snapshot["version"] == 1 and snapshot["root"]["children"][0]["transform"][12] == 0.75
    report = json.loads((run / "scripted-output/report/manifest.json").read_text())
    assert report["parameters"]["time_step_seconds"] == "0.25"
    assert report["parameters"]["simulation_frames"] == "3" and report["parameters"]["simulation_seconds"] == "0.75"
    assert report["parameters"]["hot_reload"] == "disabled" and report["parameters"]["gui"] == "disabled"
    assert report["parameters"]["scripts"] == "1" and report["parameters"]["script_0_hash_fnv1a64"]
    slower = script_args.copy()
    slower[slower.index("--time-step") + 1] = "0.5"
    invoke("scripted-step", slower + ["--output", str(run / "scripted-step")])
    assert (run / "scripted-output/final.png").read_bytes() != (run / "scripted-step/final.png").read_bytes()
    different_warmup = script_args.copy()
    different_warmup[different_warmup.index("--frames") + 1] = "1"
    different_warmup[different_warmup.index("--warmup") + 1] = "2"
    invoke("scripted-warmup", different_warmup + ["--output", str(run / "scripted-warmup")])
    assert (run / "scripted-output/final.png").read_bytes() == (run / "scripted-warmup/final.png").read_bytes()
    for index, value in enumerate(("0", "-1", "nan", "inf")):
        invalid_step = script_args.copy()
        invalid_step[invalid_step.index("--time-step") + 1] = value
        invoke(f"invalid-step-{index}", invalid_step + ["--output", str(run / f"invalid-step-{index}")], 1)
        assert not (run / f"invalid-step-{index}").exists()
    (scripted / "logic.lua").write_text("return { _process = function() error('QA update failure') end }\n")
    failed = invoke("scripted-failure", script_args + ["--output", str(run / "scripted-failure")], 1)
    assert "QA update failure" in failed and not (run / "scripted-failure").exists()
    (scripted / "logic.lua").write_text(logic, encoding="utf-8")
    invoke("scripted-recovery", script_args + ["--output", str(run / "scripted-recovery")])
    assert (run / "scripted-output/final.png").read_bytes() == (run / "scripted-recovery/final.png").read_bytes()

    project = root / "resources/research.vproject"
    invoke("project", common + ["--project", str(project), *definition, "--output", str(run / "project")])
    delivery = run / "delivery"
    delivery.mkdir()
    shipped = delivery / binary.name
    shutil.copy2(binary, shipped)
    packer = binary.with_name("vultra-pack" + binary.suffix)
    invoke("pack", [str(project), str(delivery / "project.vpk")], executable=packer)
    shipped_graph = delivery / "color_gain.vgraph"
    shutil.copy2(root / "examples/research/color_gain.vgraph", shipped_graph)
    invoke("standalone-vpk", common + [
        "--project", "project.vpk", "--graph", "color_gain.vgraph", "--output", str(run / "vpk"),
    ], executable=shipped, cwd=delivery)
    assert (run / "project/final.png").read_bytes() == (run / "vpk/final.png").read_bytes()
    assert (run / "project/scene_hdr.pfm").read_bytes() == (run / "vpk/scene_hdr.pfm").read_bytes()
    invoke("lighting-project", common + [
        "--project", str(root / "resources/research_lighting.vproject"), *definition,
        "--output", str(run / "lighting"),
    ])
    invoke("lighting-pack", [str(root / "resources/research_lighting.vproject"), str(delivery / "lighting.vpk")],
           executable=packer)
    invoke("lighting-standalone-vpk", common + [
        "--project", "lighting.vpk", "--graph", "color_gain.vgraph", "--output", str(run / "lighting-vpk"),
    ], executable=shipped, cwd=delivery)
    assert (run / "lighting/final.png").read_bytes() == (run / "lighting-vpk/final.png").read_bytes()
    assert (run / "lighting/scene_hdr.pfm").read_bytes() == (run / "lighting-vpk/scene_hdr.pfm").read_bytes()
    invoke("scripted-pack", [str(script_file), str(delivery / "scripted.vpk")], executable=packer)
    invoke("scripted-standalone-vpk", common + [
        "--project", "scripted.vpk", "--time-step", "0.25", "--output", str(run / "scripted-vpk"),
    ], executable=shipped, cwd=delivery)
    assert (run / "scripted-output/final.png").read_bytes() == (run / "scripted-vpk/final.png").read_bytes()
    assert (run / "scripted-output/scene_hdr.pfm").read_bytes() == (run / "scripted-vpk/scene_hdr.pfm").read_bytes()

    # These prebuilt fixtures use the same generated ABI and safe language APIs as player scripts.
    # Building them is a development prerequisite, never an operation performed by the delivered tool.
    native_name = "test-native-scene-script.dll" if os.name == "nt" else "libtest-native-scene-script.so"
    native_source = binary.with_name(native_name)
    assert native_source.is_file(), "Build test-scene-rendering before running multilingual offline QA"
    shutil.copy2(native_source, scripted / native_name)
    managed = root / "build/.tmp/scripting-managed"
    managed_files = (
        "Vultra.SceneScripts.dll", "Vultra.ManagedHost.dll", "Vultra.Scripting.dll",
        "Vultra.ManagedHost.runtimeconfig.json", "Vultra.SceneScripts.deps.json", "Vultra.ManagedHost.deps.json",
    )
    for filename in managed_files:
        shutil.copy2(managed / filename, scripted / filename)
    shutil.copy2(root / "tests/scene_scripts/lighting.lua", scripted / "lighting.lua")
    language_scripts = (
        ("lua", "lighting.lua", ""),
        ("native", native_name, "LightingController"),
        ("csharp", "Vultra.SceneScripts.dll", "VultraTests.LightingController"),
    )
    for language, filename, type_name in language_scripts:
        script_project["scripts"] = [{"language": language, "path": filename, "node": scene["root"]["id"]}]
        if type_name:
            script_project["scripts"][0]["type"] = type_name
        script_file.write_text(json.dumps(script_project), encoding="utf-8")
        invoke(f"{language}-project", script_args + ["--output", str(run / f"{language}-project")])
        invoke(f"{language}-pack", [str(script_file), str(delivery / f"{language}.vpk")], executable=packer)
        invoke(f"{language}-standalone-vpk", common + [
            "--project", f"{language}.vpk", "--time-step", "0.25", "--output", str(run / f"{language}-vpk"),
        ], executable=shipped, cwd=delivery)
        for filename in ("final.png", "scene_hdr.pfm"):
            assert (run / f"{language}-project" / filename).read_bytes() == (run / f"{language}-vpk" / filename).read_bytes()
            assert (run / "lua-project" / filename).read_bytes() == (run / f"{language}-project" / filename).read_bytes()
    print(f"Offline QA outputs: {run}")


if __name__ == "__main__":
    main()
