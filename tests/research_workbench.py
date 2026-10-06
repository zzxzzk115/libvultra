"""Validate the real workbench offscreen, its saved document and batch image parity."""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    root = Path(__file__).resolve().parents[1]
    temporary = root / "build/.tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="workbench-qa-", dir=temporary))
    environment = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "VULTRA_WINDOW_SYSTEM"):
        environment.pop(name, None)
    environment["PATH"] = str(run / "no-build-tools")

    def invoke(name, options, expected=0, executable=binary):
        result = subprocess.run(
            [str(executable), *options], cwd=root, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90,
        )
        (run / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        assert result.returncode == expected, f"{name}: {result.stdout}"
        if expected == 0:
            assert "[error]" not in result.stdout and "[warning]" not in result.stdout, result.stdout
        print(f"{name}: passed", flush=True)
        return result.stdout

    common = ["--offline", "--frames", "3"]
    invoke("first", common + ["--export", str(run / "first")])
    invoke("reopened", common + [
        "--workspace", str(run / "first/workspace.vworkspace"), "--export", str(run / "reopened"),
    ])
    for name in ("final.png", "final.pfm", "output_000.png", "output_000.pfm", "graph.vgraph"):
        assert (run / "first" / name).read_bytes() == (run / "reopened" / name).read_bytes(), name
    display_workspace = json.loads((run / "first/workspace.vworkspace").read_text())
    display_workspace["project"] = str(root / "resources/research.vproject")
    display_graph = display_workspace["graph"]
    display_graph["passes"].append({"id": "tone", "type": "vultra.tone_mapping", "parameters": {}})
    display_graph["edges"].append({"from": "gain.color", "to": "tone.hdr"})
    display_graph["outputs"] = ["tone.color", "gain.color"]
    display_file = run / "display.vworkspace"
    display_file.write_text(json.dumps(display_workspace), encoding="utf-8")
    invoke("explicit-display", common + ["--workspace", str(display_file), "--export", str(run / "display")])
    assert (run / "display/final.png").read_bytes() == (run / "first/final.png").read_bytes()
    display_report = json.loads((run / "display/graph_report.json").read_text())
    assert any(event["name"] == "tone" for event in display_report["events"])
    assert all(event["name"] != "Tone mapping" for event in display_report["events"])
    reference_workspace = json.loads((run / "first/workspace.vworkspace").read_text())
    reference_workspace["project"] = str(root / "resources/research.vproject")
    reference_workspace["extent"] = [128, 96]
    reference_workspace["renderer"]["path"] = 2
    reference_workspace["seed"] = 71
    reference_file = run / "reference.vworkspace"
    reference_file.write_text(json.dumps(reference_workspace), encoding="utf-8")
    invoke("reference", common + ["--workspace", str(reference_file), "--export", str(run / "reference")])
    invoke("reference-reopened", common + ["--workspace", str(run / "reference/workspace.vworkspace"),
                                           "--export", str(run / "reference-reopened")])
    assert (run / "reference/final.png").read_bytes() == (run / "reference-reopened/final.png").read_bytes()
    reference_outputs = json.loads((run / "reference/manifest.json").read_text())["outputs"]
    assert {"scene." + name for name in ("radiance", "albedo", "normal", "depth", "motion", "sample_count", "ray_count")} <= {
        entry["port"] for entry in reference_outputs}
    report = json.loads((run / "reference/graph_report.json").read_text())
    assert report["version"] == 1 and report["graph_owned_bytes"] > 0
    assert any(resource["history"] for resource in report["resources"])
    assert report["events"] and all(image["producers"] for image in report["images"])
    screenshot = (run / "first/workbench.png").read_bytes()
    assert screenshot[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack(">II", screenshot[16:24]) == (1600, 1000)
    manifest = json.loads((run / "first/manifest.json").read_text())
    assert manifest["version"] == 1 and manifest["outputs"][1]["port"] == "gain.color"
    definition = json.loads((run / "first/graph.vgraph").read_text())
    definition["outputs"] += ["scene.hdr", "scene.normal_roughness", "scene.depth"]
    graph_file = run / "marked.vgraph"
    graph_file.write_text(json.dumps(definition), encoding="utf-8")
    invoke("marked", common + ["--graph", str(graph_file), "--export", str(run / "marked")])
    marked = json.loads((run / "marked/manifest.json").read_text())
    assert len(marked["outputs"]) == 5 and marked["outputs"][-1]["port"] == "scene.depth"
    batch = binary.with_name("vultra-batch" + binary.suffix)
    invoke("batch", [
        "--project", str(root / "resources/research.vproject"),
        "--graph", str(root / "examples/research/color_gain.vgraph"),
        "--width", "640", "--height", "360", "--frames", "3",
        "--revision", "qa-working-tree", "--output", str(run / "batch"),
    ], executable=batch)
    assert (run / "first/final.png").read_bytes() == (run / "batch/final.png").read_bytes()
    assert (run / "first/output_000.pfm").read_bytes() == (run / "batch/scene_hdr.pfm").read_bytes()
    lighting_project = str(root / "resources/research_lighting.vproject")
    invoke("lighting", common + ["--project", lighting_project, "--export", str(run / "lighting")])
    invoke("lighting-reopened", common + [
        "--workspace", str(run / "lighting/workspace.vworkspace"), "--export", str(run / "lighting-reopened"),
    ])
    invoke("lighting-batch", [
        "--project", lighting_project, "--graph", str(root / "examples/research/color_gain.vgraph"),
        "--width", "640", "--height", "360", "--frames", "3",
        "--revision", "qa-working-tree", "--output", str(run / "lighting-batch"),
    ], executable=batch)
    assert (run / "lighting/final.png").read_bytes() == (run / "lighting-reopened/final.png").read_bytes()
    assert (run / "lighting/final.png").read_bytes() == (run / "lighting-batch/final.png").read_bytes()
    assert (run / "lighting/output_000.pfm").read_bytes() == (run / "lighting-batch/scene_hdr.pfm").read_bytes()
    stages = json.loads((root / "examples/research/deferred.vgraph").read_text())
    post = json.loads((run / "lighting/graph.vgraph").read_text())
    stages["passes"] += post["passes"]
    stages["edges"] += [{"from": edge["from"].replace("scene.hdr", "lighting.hdr"), "to": edge["to"]}
                        for edge in post["edges"]]
    stages["outputs"] = post["outputs"] + stages["outputs"]
    stages_file = run / "deferred-with-post.vgraph"
    stages_file.write_text(json.dumps(stages))
    invoke("lighting-stages", common + [
        "--project", lighting_project, "--graph", str(stages_file),
        "--export", str(run / "lighting-stages"),
    ])
    invoke("lighting-stages-reopened", common + [
        "--workspace", str(run / "lighting-stages/workspace.vworkspace"),
        "--export", str(run / "lighting-stages-reopened"),
    ])
    assert (run / "lighting-stages/final.png").read_bytes() == (run / "lighting/final.png").read_bytes()
    assert (run / "lighting-stages/final.png").read_bytes() == (run / "lighting-stages-reopened/final.png").read_bytes()
    staged_report = json.loads((run / "lighting-stages/graph_report.json").read_text())
    assert any(event["name"] == "G-buffer geometry" for event in staged_report["events"])
    lighting_report = json.loads((run / "lighting-batch/report/manifest.json").read_text())
    eye = [float(value) for value in lighting_report["parameters"]["camera_eye"].split(",")]
    assert eye == [0, 0.3, 3], "Report did not record the scene camera"
    assert lighting_report["parameters"]["environment_intensity"] == "1.000000"
    dim_workspace = json.loads((run / "lighting/workspace.vworkspace").read_text())
    scene = dim_workspace["scene"]
    selected = scene["current_environment"]
    assert selected is not None and scene["version"] == 1
    environment_node = next(node for node in scene["root"]["children"] if node["id"] == selected)
    environment_node["environment"]["intensity"] = 0.25
    dim_file = run / "lighting/dim.vworkspace"
    dim_file.write_text(json.dumps(dim_workspace), encoding="utf-8")
    invoke("environment-dim", common + ["--workspace", str(dim_file), "--export", str(run / "environment-dim")])
    invoke("environment-dim-reopened", common + [
        "--workspace", str(run / "environment-dim/workspace.vworkspace"),
        "--export", str(run / "environment-dim-reopened"),
    ])
    assert (run / "environment-dim/final.png").read_bytes() != (run / "lighting/final.png").read_bytes()
    assert (run / "environment-dim/final.png").read_bytes() == (
        run / "environment-dim-reopened/final.png").read_bytes()
    assert (run / "environment-dim/output_000.pfm").read_bytes() == (
        run / "environment-dim-reopened/output_000.pfm").read_bytes()
    assert "positive --frames" in invoke("zero", [
        "--offline", "--frames", "0", "--export", str(run / "zero"),
    ], 1)
    assert "new output directory" in invoke("overwrite", common + ["--export", str(run / "first")], 1)
    definition["edges"][0]["from"] = "scene.missing"
    graph_file.write_text(json.dumps(definition), encoding="utf-8")
    assert "scene.missing" in invoke("invalid", common + [
        "--graph", str(graph_file), "--export", str(run / "invalid"),
    ], 1)
    assert not (run / "invalid").exists()
    print(f"Workbench QA outputs: {run}")


if __name__ == "__main__":
    main()
