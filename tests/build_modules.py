"""Build and exercise independent Vultra feature selections without a desktop window."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
PROJECT = ROOT / "tests/modules"
PACKAGE_FEATURES = {
    "vri": {"gpu"}, "glm": {"math"}, "nlohmann_json": {"json"}, "stb": {"image_io"},
    "slang-static": {"shader"}, "imgui": {"editor_gui"}, "rmlui": {"vgui"},
    "xxhash": {"shader_runtime", "asset_source"}, "antlr4-runtime": {"game_shader"},
    "libsdl3": {"window"}, "meshoptimizer": {"meshlet_build"},
    "directxtex": {"texture_import"}, "vtask": {"asset_jobs"}, "ispc": {"texture_import"},
    "tinygltf": {"asset_import"}, "tinyobjloader": {"asset_import"}, "openfbx": {"asset_import"},
    "nativefiledialog-extended": {"dialogs"},
    "openxr-static": {"xr"}, "vulkan-headers": {"xr"},
}


def verify_sources(features, sources):
    asset_files = {
        "asset_source": {"asset_source.cpp", "source_file.cpp", "vpk_archive.cpp"},
        "builtin_pack": {"builtin_pack.cpp"},
        "asset_values": {"material_parameters.cpp", "material_properties.generated.cpp", "tangents.cpp"},
        "material_values": {"material_instance.cpp", "shader_property.cpp"},
        "asset_jobs": {"import_jobs.cpp"},
        "meshlet_build": {"meshlet_build.cpp"},
        "texture_import": {"dds_loader.cpp", "image_decode.cpp", "texture_import.cpp"},
        "asset_import": {"asset_options.cpp", "asset_pipeline.cpp", "fbx_loader.cpp", "gltf_loader.cpp", "obj_loader.cpp"},
    }
    allowed_assets = {file for feature, files in asset_files.items() if feature in features for file in files}
    for file in sources:
        path = Path(file)
        if any(part in path.parts for part in ("extensions", "scripting")) or "/source/scene/" in file:
            raise RuntimeError(f"Library compiled application code: {file}")
        if "/api/" in file:
            owner = {"vultra_ui.generated.cpp": "editor_gui",
                     "vultra_compute.generated.cpp": "compute"}.get(path.name)
            inspector = path.name == "render_settings.generated.cpp" and {"editor_gui", "rendering"} <= features
            if owner not in features and not inspector:
                raise RuntimeError(f"Compiled an unselected ABI domain: {file}")
        if "/source/assets/" in file:
            cooked_source = "/assets/src/shaders/" in file and path.name in {
                "shader_asset.cpp", "shader_archive.cpp", "material_instance.cpp"}
            game_source = "/assets/src/shaders/" in file and (
                "game_shader" in features or ("game_shader_runtime" in features and cooked_source))
            if not game_source and path.name not in allowed_assets:
                raise RuntimeError(f"Compiled an unselected asset implementation: {file}")
        if "/source/ui/" in file:
            owner = "vgui" if path.name in ("vgui.cpp", "vgui_skin.png") else "editor_gui"
            if path.name == "editor_gui_shader_material.cpp":
                owner = "shader_inspector"
            if owner not in features:
                raise RuntimeError(f"Compiled an unselected UI implementation: {file}")
        if path.name == "random_stream.cpp" and "random" not in features:
            raise RuntimeError(f"Compiled unselected random streams: {file}")
        if "/drivers/src/rhi/" in file and path.name.startswith("shader"):
            runtime_sources = {"shader_program.cpp", "shader_archive.cpp", "shader_parameters.cpp",
                               "shader_bindings.cpp", "shader_pipeline.cpp"}
            owner = "shader_runtime" if path.name in runtime_sources else "shader"
            if owner not in features:
                raise RuntimeError(f"Compiled an unselected shader implementation: {file}")
        if "/servers/" in file:
            owner = "graph" if "/graph/" in file else "rendering"
            if path.name == "texture_blit.cpp":
                owner = "blit"
            elif path.name in {"shader_material.cpp", "shader_pass_state.cpp", "shader_runtime.cpp"}:
                owner = "shader_material"
            elif path.name == "shader_source_reload.cpp":
                owner = "game_shader"
            elif path.name in {"meshlets.cpp", "rendering_server.cpp", "scene.cpp"}:
                owner = "gpu_scene"
            elif path.name == "texture_upload.cpp":
                owner = "texture_upload"
            elif path.name == "scene_prepare.cpp":
                owner = "scene_prepare"
            if owner not in features:
                raise RuntimeError(f"Compiled an unselected server implementation: {file}")


def run(arguments, log, *, succeeds=True, env=None, cwd=PROJECT):
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(arguments, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
    if (result.returncode == 0) != succeeds:
        raise RuntimeError(f"Unexpected command result: {arguments}\n{log.read_text()}")
    return log.read_text()


def verify_dependencies(executable, features, log, env):
    if sys.platform != "linux":
        return
    dynamic = run(["readelf", "-d", str(executable)], log, env=env)
    window_libraries = ("libX11", "libxcb", "libwayland", "libSDL")
    if "window" not in features and any(library in dynamic for library in window_libraries):
        raise RuntimeError(f"{executable.name} retained unused desktop libraries: {dynamic}")
    if "gpu" not in features and "libvulkan" in dynamic:
        raise RuntimeError(f"{executable.name} retained a GPU dependency: {dynamic}")


def verify_diagnostics(output, features, target):
    if "[error]" in output or "Validation Error" in output:
        raise RuntimeError(f"{target} emitted unexpected diagnostics: {output}")


def main():
    temporary = ROOT / "build/.tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix="modules-", dir=temporary))
    env = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "VULTRA_MODULE_TEST_WINDOW"):
        env.pop(name, None)
    cases = {
        "foundation": "foundation",
        "random": "random",
        "gpu": None,  # The embedded-project default must really be GPU-only.
        "graph": "graph",
        "graph_json": "graph_json",
        "image_io": "image_io",
        "properties": "properties",
        "desktop": "desktop",
        "shader_runtime": "shader_runtime",
        "shader": "shader",
        "shader_graph": "shader,graph",
        "shader_graph_json": "shader,graph_json",
        "asset_source": "asset_source",
        "game_shader_runtime": "game_shader_runtime",
        "shader_material": "shader_material",
        "shader_material_source": "shader_material,game_shader",
        "shader_inspector": "shader_inspector,image_io",
        "game_shader": "game_shader",
        "editor_gui": "editor_gui,image_io",
        "editor_gui_window": "editor_gui,window,image_io",
        "vgui": "vgui",
        "vgui_window": "vgui,window",
        "material_values": "material_values",
        "texture_values": "texture_values",
        "texture_upload": "texture_upload",
        "gpu_scene": "gpu_scene",
        "meshlet_build": "meshlet_build",
        "scene_prepare": "scene_prepare",
        "asset_values": "asset_values",
        "camera": "camera",
        "texture_import": "texture_import",
        "asset_import": "asset_import",
        "rendering": "rendering",
        "rendering_source": "rendering,scene_prepare,shader",
        "interactive": "interactive",
    }
    if len(sys.argv) > 1:
        requested = sys.argv[1:]
        unknown = set(requested) - cases.keys()
        if unknown:
            raise ValueError(f"Unknown module cases: {sorted(unknown)}")
        cases = {name: cases[name] for name in requested}
    builtin_pack = None
    if set(cases) & {"rendering", "vgui", "vgui_window"}:
        host = evidence / "host"
        host.mkdir()
        host_env = env | {"XMAKE_CONFIGDIR": str(host)}
        run(["xmake", "f", "-P", str(PROJECT), "-F", str(PROJECT / "xmake.lua"), "-y", "-m", "release",
             "-o", str(host / "build"), "--libvultra_features=builtin_pack",
             "--libvultra_build_examples=n", "--libvultra_build_tests=n"],
            host / "configure.log", env=host_env)
        run(["xmake", "build", "-P", str(PROJECT), "-F", str(PROJECT / "xmake.lua"), "-y", "module-builtin-pack"],
            host / "build.log", env=host_env)
        cookers = [file for file in (host / "build").rglob("module-builtin-pack*")
                   if file.is_file() and file.name in ("module-builtin-pack", "module-builtin-pack.exe")]
        if len(cookers) != 1:
            raise RuntimeError(f"Expected one host builtin cooker: {cookers}")
        builtin_pack = host / "builtin.vpk"
        run([str(cookers[0]), str(ROOT), str(builtin_pack)], host / "cook.log", env=host_env, cwd=ROOT)
    for name, selection in cases.items():
        directory = evidence / name
        directory.mkdir()
        build = directory / "build"
        case_env = env | {"XMAKE_CONFIGDIR": str(directory)}
        configure = ["xmake", "f", "-P", str(PROJECT), "-F", str(PROJECT / "xmake.lua"),
                     "-y", "-c", "-m", "release", "-o", str(build),
                     "--libvultra_window_backend=sdl3"]
        if selection:
            configure.append("--libvultra_features=" + selection)
        run(configure, directory / "configure.log", env=case_env)
        run(["xmake", "build", "-P", str(PROJECT), "-F", str(PROJECT / "xmake.lua"), "-y"],
            directory / "build.log", env=case_env)
        manifests = list(build.rglob("modules.json"))
        if len(manifests) != 1:
            raise RuntimeError(f"Missing/unexpected module report for {name}: {manifests}")
        manifest = json.loads(manifests[0].read_text())
        packages = set(manifest["packages"])
        features = set(manifest["features"])
        if {"runtime", "editor", "tools"} & features:
            raise RuntimeError(f"{name} selected unrelated standalone consumers: {features}")
        expected_targets = {"module-probe", "vultra"}
        if "capture" in features:
            expected_targets.add("renderdoc-api")
        if "shader" in features:
            expected_targets.add("filewatch")
        if "asset_source" in features:
            expected_targets.update(("module-assets", "module-vpk-memory"))
        if "editor_gui" in features and "image_io" in features:
            expected_targets.update(("module-gui", "module-gui-lifetime"))
        if "vgui" in features:
            expected_targets.add("module-vgui")
        if "game_shader" in features:
            expected_targets.add("module-game-shader")
        if "meshlet_build" in features:
            expected_targets.add("module-meshlet-build")
        if "properties" in features:
            expected_targets.add("module-property-schema")
        if "random" in features:
            expected_targets.update(("pcg32", "module-random"))
        if "rendering" in features:
            expected_targets.add("openpbr")
            expected_targets.add("module-rendering" if {"scene_prepare", "shader"} <= features else "module-rendering-runtime")
        if "texture_import" in features:
            expected_targets.add("bc7enc")
        if "asset_import" in features:
            expected_targets.add("module-import")
        if "material_values" in features:
            expected_targets.add("module-material")
        if set(manifest["targets"]) != expected_targets:
            raise RuntimeError(f"{name} registered unrelated targets: {manifest['targets']}")
        expected_packages = {"argparse", "spdlog"} | {
            package for package, owners in PACKAGE_FEATURES.items() if owners & features
        }
        if {"shader_material", "game_shader"} <= features:
            expected_packages.add("vtask")
        if packages != expected_packages:
            raise RuntimeError(f"{name} package mismatch: expected {expected_packages}; configured {packages}")
        source_paths = [Path(file).as_posix() for file in manifest["sources"]]
        verify_sources(features, source_paths)
        binaries = [file for file in build.rglob("module-probe*") if file.is_file() and file.suffix in ("", ".exe")]
        if len(binaries) != 1:
            raise RuntimeError(f"Missing/unexpected binary for {name}: {binaries}")
        verify_dependencies(binaries[0], features, directory / "dynamic.log", case_env)
        run([str(binaries[0]), str(directory / "output")], directory / "run.log", env=case_env, cwd=ROOT)
        diagnostics = (directory / "run.log").read_text()
        verify_diagnostics(diagnostics, features, "module-probe")
        for target in sorted(target for target in expected_targets
                             if target.startswith("module-") and target != "module-probe"
                             and manifest["target_kinds"][target] == "binary"):
            executable = binaries[0].with_name(target + binaries[0].suffix)
            arguments = [str(executable)]
            if target in ("module-assets", "module-vgui", "module-import"):
                arguments.append(str(directory / target))
            if target == "module-vgui":
                arguments.append(str(builtin_pack))
            if target == "module-rendering-runtime":
                arguments.extend([str(builtin_pack), str(directory / target)])
            output = run(arguments, directory / (target + ".log"), env=case_env, cwd=ROOT)
            verify_diagnostics(output, features, target)
            verify_dependencies(executable, features, directory / (target + "-dynamic.log"), case_env)
        print(f"{name}: {len(source_paths)} source files, {sorted(packages)}; module checks passed", flush=True)

    if "shader_graph" in cases and "shader_graph_json" in cases:
        direct = (evidence / "shader_graph/output/compute.rgba").read_bytes()
        persisted = (evidence / "shader_graph_json/output/compute.rgba").read_bytes()
        if direct != persisted:
            raise RuntimeError("Direct and JSON compute graphs produced different image readbacks")

    for selection, expected, flags in (
        ("not_a_feature", "Unknown libvultra feature", []),
        ("xr", "requires libvultra_with_openxr=true", []),
        ("foundation", "Repository tests require feature", ["--libvultra_build_tests=y"]),
    ):
        log = evidence / (selection + "-rejected.log")
        output = run(["xmake", "f", "-P", str(PROJECT), "-F", str(PROJECT / "xmake.lua"), "-y", "-c",
                      "-o", str(evidence / selection), "--libvultra_features=" + selection,
                      "--libvultra_with_openxr=n"] + flags, log, succeeds=False,
                     env=env | {"XMAKE_CONFIGDIR": str(evidence / selection)})
        if expected not in output:
            raise RuntimeError(f"Wrong feature-selection failure: {output}")
    print(f"Feature closure and rejection evidence: {evidence}", flush=True)


if __name__ == "__main__":
    main()
