"""Compare safe NumPy experiment sessions with the standalone batch renderer."""

import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading

import numpy as np

from dataclasses import replace

from vultra import CameraSettings, LightSettings, Research, ResearchError
from vultra.research import _Host
from vultra._bindings_generated import ABI_VERSION, VultraExperimentApi


def read_pfm(file):
    with file.open("rb") as stream:
        assert stream.readline() == b"PF\n"
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        assert abs(scale) == 1
        image = np.frombuffer(stream.read(), dtype="<f4" if scale < 0 else ">f4")
    return image.reshape(height, width, 3)[::-1].copy()


def rejected(operation, message):
    try:
        operation()
    except (ResearchError, ValueError) as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError("Expected rejected operation: " + message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("batch", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    run = Path(tempfile.mkdtemp(prefix="python-qa-", dir=root / "build/.tmp"))
    library = args.library.resolve()
    batch = args.batch.resolve()
    environment = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "VULTRA_WINDOW_SYSTEM"):
        environment.pop(name, None)
    environment["PATH"] = str(run / "no-build-tools")

    def invoke(name, executable, options, cwd=root):
        result = subprocess.run(
            [str(executable), *map(str, options)], cwd=cwd, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120,
        )
        (run / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        assert result.returncode == 0, result.stdout
        assert "[error]" not in result.stdout and "[warning]" not in result.stdout, result.stdout

    project = root / "resources/research_lighting.vproject"
    definition = json.loads((root / "examples/research/color_gain.vgraph").read_text())
    with Research(library) as host:
        description_output = run / "description-cli"
        invoke("description", batch, [
            "--experiment", root / "resources/research.vexperiment", "--revision", "qa-working-tree",
            "--output", description_output,
        ])
        description = description_output / "experiment.vexperiment"
        with host.run(description) as session:
            assert session.progress.frames == 4
            np.testing.assert_array_equal(session.image("hdr")[:, :, :3],
                                          read_pfm(description_output / "scene_hdr.pfm"))
        invoke("description-replay", batch, [
            "--experiment", description, "--revision", "qa-working-tree", "--output", run / "description-replay",
        ])
        assert (description_output / "final.png").read_bytes() == (run / "description-replay/final.png").read_bytes()
        invalid_description = json.loads(description.read_text())
        invalid_description["version"] = 2
        invalid_file = run / "invalid.vexperiment"
        invalid_file.write_text(json.dumps(invalid_description))
        rejected(lambda: host.run(invalid_file), "version 1")
        with host.run(description) as session:
            assert session.progress.frames == 4
        display_description = json.loads(description.read_text())
        display_graph = display_description["graph"]
        display_graph["passes"].append({"id": "display", "type": "vultra.tone_mapping", "parameters": {}})
        display_graph["edges"].append({"from": "gain.color", "to": "display.hdr"})
        display_graph["outputs"] = ["display.color", "gain.color"]
        display_file = description_output / "display.vexperiment"
        display_file.write_text(json.dumps(display_description))
        invoke("explicit-display", batch, [
            "--experiment", display_file, "--revision", "qa-working-tree", "--output", run / "explicit-display",
        ])
        assert (description_output / "final.png").read_bytes() == (run / "explicit-display/final.png").read_bytes()
        with host.run(display_file) as session:
            assert all(event["name"] != "Tone mapping" for event in session.report["events"])
            np.testing.assert_array_equal(session.image("display.color"), session.image("final"))
        print("description: CLI/Python replay parity and failed-input recovery", flush=True)
        # Reject stale version-1 layouts before constructing another GPU device.
        stale = _Host()
        assert host._create(ABI_VERSION + 1, ctypes.sizeof(VultraExperimentApi), 0, ctypes.byref(stale)) != 0
        assert host._create(ABI_VERSION, ctypes.sizeof(VultraExperimentApi) - 1, 0, ctypes.byref(stale)) != 0
        assert stale.owner is None
        rejected(lambda: host.open(project, width=0), "width")
        rejected(lambda: host.open(project, time_step=float("nan")), "time_step")
        with host.open(project, width=65, height=49) as session:
            rejected(lambda: session.image(), "completed frame")
            session.set_graph(definition)
            for index, gain in enumerate((0.25, 0.5, 1.0)):
                session.set_parameter("gain", "gain", gain)
                session.step(2)
                image = session.image("hdr")
                assert image.shape == (49, 65, 4) and image.dtype == np.float32
                assert image.flags.owndata and image.flags.c_contiguous and np.isfinite(image).all()
                output = run / f"gain-{index}"
                invoke(f"gain-{index}", batch, [
                    "--project", project, "--width", 65, "--height", 49,
                    "--frames", 2, "--warmup", 0, "--revision", "qa-working-tree",
                    "--color-gain", gain, "--output", output,
                ])
                np.testing.assert_array_equal(image[:, :, :3], read_pfm(output / "scene_hdr.pfm"))
                print(f"parameter {gain}: exact NumPy/CLI HDR parity", flush=True)
            assert session.progress.frames == 6 and session.progress.seconds > 0
            previous = session.image("hdr")
            invalid = json.loads(json.dumps(definition))
            invalid["edges"][0]["from"] = "scene.missing"
            rejected(lambda: session.set_graph(invalid), "scene.missing")
            np.testing.assert_array_equal(session.image("hdr"), previous)
            rejected(lambda: session.set_parameter("gain", "gain", -1), "gain")
            session.step()
            np.testing.assert_array_equal(session.image("hdr"), previous)
            assert session.scene["version"] == 1
            errors = []

            def other_thread():
                try:
                    session.step()
                except ResearchError as error:
                    errors.append(str(error))

            thread = threading.Thread(target=other_thread)
            thread.start()
            thread.join()
            assert errors and "creating thread" in errors[0]
        rejected(lambda: session.step(), "closed")
    np.testing.assert_array_equal(previous, image)
    assert previous.flags.owndata and np.isfinite(previous).all()
    rejected(lambda: host.open(project), "closed")

    with Research(library, ray_tracing=True) as host:
        with host.open(project, width=33, height=25, path="reference", seed=71) as session:
            session.set_graph(definition)
            session.step(4)
            output = run / "reference"
            invoke("reference", batch, [
                "--project", project, "--width", 33, "--height", 25,
                "--path", "reference", "--seed", 71, "--frames", 4, "--warmup", 0,
                "--revision", "qa-working-tree", "--graph", root / "examples/research/color_gain.vgraph",
                "--output", output,
            ])
            np.testing.assert_array_equal(session.image("hdr")[:, :, :3], read_pfm(output / "scene_hdr.pfm"))
            for name in ("radiance", "albedo", "normal", "depth", "motion", "sample_count", "ray_count"):
                assert np.isfinite(session.image("scene." + name)).all()
            assert np.all(session.image("scene.sample_count")[:, :, 0] == 4)
            report = session.save_report(run / "reference-python", revision="qa-working-tree",
                                         outputs=("hdr", "scene.radiance", "scene.depth"))
            assert report["version"] == 1 and report["experiment"]["seed"] == 71
            assert report["experiment"]["frames"] == 4 and report["graph_owned_bytes"] > 0
            assert report["images"][1]["producers"]
            np.testing.assert_array_equal(read_pfm(run / "reference-python/output_000.pfm"),
                                          read_pfm(output / "scene_hdr.pfm"))
            print("reference: exact Python/CLI HDR, seven AOVs and exported graph report", flush=True)

    with Research(library) as host, host.open(project, width=65, height=49) as session:
        session.step(2)
        original = session.image("hdr")
        camera = session.find_node("Camera")
        settings = session.camera_settings(camera)
        assert type(settings) is CameraSettings
        session.set_camera_settings(camera, replace(settings, vertical_fov=settings.vertical_fov * 0.7))
        session.step()
        assert not np.array_equal(session.image("hdr"), original)
        session.set_camera_settings(camera, settings)
        session.step()
        np.testing.assert_array_equal(session.image("hdr"), original)
        rejected(lambda: session.set_camera_settings(camera, replace(settings, near_plane=settings.far_plane)),
                 "near < far")
        assert session.camera_settings(camera) == settings
        matrix = session.transform(camera)
        moved = matrix.copy()
        moved[0, 3] += 0.2
        session.set_transform(camera, moved)
        session.step()
        assert not np.array_equal(session.image("hdr"), original)
        session.set_transform(camera, matrix)
        session.step()
        np.testing.assert_array_equal(session.image("hdr"), original)
        nodes = []
        def collect(node):
            nodes.append(node)
            for child in node.get("children", ()):
                collect(child)
        collect(session.scene["root"])
        light = next(node["id"] for node in nodes if "light" in node)
        light_settings = session.light_settings(light)
        assert type(light_settings) is LightSettings
        session.set_light_settings(light, replace(light_settings, intensity=0))
        session.step()
        assert not np.array_equal(session.image("hdr"), original)
        session.set_light_settings(light, light_settings)
        environment_id = session.scene["current_environment"]
        environment_settings = session.environment_settings(environment_id)
        session.set_environment_settings(environment_id, replace(environment_settings, intensity=0))
        session.step()
        assert not np.array_equal(session.image("hdr"), original)
        session.set_environment_settings(environment_id, environment_settings)
        rejected(lambda: session.set_environment_settings(environment_id,
                 replace(environment_settings, intensity=float("nan"))), "finite numeric")
        material = session.scene["materials"][0]["id"]
        material_settings = session.material_parameters(material)
        session.set_material_parameters(material, replace(material_settings, base_red=0.05, coat_weight=0.5))
        session.step()
        assert not np.array_equal(session.image("hdr"), original)
        session.set_material_parameters(material, material_settings)
        session.step()
        np.testing.assert_array_equal(session.image("hdr"), original)
        rgba = session.probe_pixel("hdr", 32, 24)
        np.testing.assert_array_equal(rgba, original[24, 32])
        preview = session.preview("hdr", channel="r", minimum=-1, maximum=2)
        mapped = np.clip((original[:, :, 0] + 1) / 3, 0, 1)
        np.testing.assert_allclose(preview[:, :, :3], np.repeat(mapped[:, :, None], 3, axis=2), atol=1e-7, rtol=0)
        np.testing.assert_array_equal(session.image("hdr"), original)
        rejected(lambda: session.preview("hdr", minimum=2, maximum=1), "range")
        rejected(lambda: session.probe_pixel("hdr", 65, 0), "outside")
        rejected(lambda: session.set_camera_settings(light, settings), "camera node")
        report = session.report
        assert report["provenance"]["import_dependencies"] and report["provenance"]["shader_files"]
        def fnv1a64(bytes_):
            value = 14695981039346656037
            for byte in bytes_:
                value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
            return f"{value:016x}"
        assert report["provenance"]["entry_scene"]["fnv1a64"] == fnv1a64(
            (root / "resources/scenes/research_lighting.vscene").read_bytes())
        shader = next(file for file in report["provenance"]["shader_files"]
                      if file["path"] == "examples/research/shaders/color_gain.slang")
        assert shader["fnv1a64"] == fnv1a64((root / shader["path"]).read_bytes())
        session.set_graph(json.loads((root / "examples/research/deferred.vgraph").read_text()))
        session.step()
        np.testing.assert_array_equal(session.image("hdr"), original)
        assert session.image("geometry.normal_roughness").shape == original.shape
        print("typed scene edits, raw probes, mapped AOVs, asset/shader provenance and built-in pass parity", flush=True)

    # A copied native library and VPK need neither xmake nor repository-relative shaders.
    delivery = run / "delivery"
    delivery.mkdir()
    shipped = delivery / library.name
    shutil.copy2(library, shipped)
    packer = batch.with_name("vultra-pack" + batch.suffix)
    invoke("pack", packer, [project, delivery / "project.vpk"])
    with Research(shipped) as host, host.open(delivery / "project.vpk", width=65, height=49) as session:
        session.set_graph(definition)
        session.set_parameter("gain", "gain", 1)
        session.step(2)
        np.testing.assert_array_equal(session.image("hdr"), previous)
    print(f"Python ownership, recovery and copied-library/VPK checks passed: {run}", flush=True)


if __name__ == "__main__":
    main()
