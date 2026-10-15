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
    "xxhash": {"shader", "asset_source"}, "antlr4-runtime": {"game_shader"},
    "libsdl3": {"window"}, "entt": {"world"}, "meshoptimizer": {"rendering"},
    "directxtex": {"texture_import"}, "vtask": {"texture_import"}, "ispc": {"texture_import"},
    "tinygltf": {"asset_import"}, "tinyobjloader": {"asset_import"}, "openfbx": {"asset_import"},
    "lua": {"script_lua"},
    "nativefiledialog-extended": {"dialogs"}, "imgui-node-editor": {"editor"},
    "openxr-static": {"xr"}, "vulkan-headers": {"xr"},
}
RESEARCH_SOURCES = {
    "research_quality": {"quality.cpp"}, "research_graph": {"graph_report.cpp"},
    "research_camera": {"camera_track.cpp"}, "research_project": {"research_project.cpp"},
    "research_reference": {"environment_sampling.cpp", "reference_path_tracer.cpp"},
    "research_experiment": {"experiment_description.cpp", "experiment_session.cpp"},
    "research_api": {"research_bridge.cpp"},
    "research_stereo": {"headset_profile.cpp", "stereo_views.cpp", "stereo_renderer.cpp", "research_configuration.cpp"},
    "research_host": {"experiment_host.cpp", "experiment_api.generated.cpp"},
}


def verify_sources(features, sources):
    asset_files = {
        "asset_source": {"asset_source.cpp", "source_file.cpp", "vpk_archive.cpp"},
        "project": {"project_manifest.cpp"},
        "asset_values": {"material_parameters.cpp", "material_properties.generated.cpp", "tangents.cpp"},
        "material_values": {"material_instance.cpp", "shader_property.cpp"},
        "texture_import": {"dds_loader.cpp", "image_decode.cpp", "import_jobs.cpp", "texture_import.cpp"},
        "asset_import": {"asset_options.cpp", "asset_pipeline.cpp", "fbx_loader.cpp", "gltf_loader.cpp", "obj_loader.cpp"},
        "asset_export": {"vpk_export.cpp"},
    }
    allowed_assets = {file for feature, files in asset_files.items() if feature in features for file in files}
    for file in sources:
        path = Path(file)
        if "/extensions/" in file:
            owners = {feature for feature, files in RESEARCH_SOURCES.items() if path.name in files}
            if "/extensions/research/" not in file or not owners & features:
                raise RuntimeError(f"Compiled an unselected extension: {file}")
            continue
        if "/scripting/" in file:
            owner = {"script_host.cpp": "script_host", "native_script.cpp": "script_native",
                     "lua_script.cpp": "script_lua", "dotnet_script.cpp": "script_csharp"}.get(path.name)
            if owner not in features:
                raise RuntimeError(f"Compiled an unselected language implementation: {file}")
        if "/api/" in file:
            owner = {"vultra_scene.generated.cpp": "api_scene", "vultra_ui.generated.cpp": "api_ui",
                     "plugin_session.cpp": "api_host", "native_plugin.cpp": "native_plugin"}.get(path.name)
            inspector = "editor_gui" in features and (
                (path.name == "scene_properties.generated.cpp" and "world" in features) or
                (path.name == "render_settings.generated.cpp" and "rendering" in features))
            if owner not in features and not inspector:
                raise RuntimeError(f"Compiled an unselected ABI domain: {file}")
        if "/assets/" in file:
            game_source = "game_shader" in features and "/assets/src/shaders/" in file
            if not game_source and path.name not in allowed_assets:
                raise RuntimeError(f"Compiled an unselected asset implementation: {file}")
        if "/scene/" in file:
            owner = "world"
            if "/camera/" in file:
                owner = "camera"
            elif path.name in ("scene_import.cpp", "scene_shader_materials.cpp"):
                owner = "scene_rendering"
            if owner not in features:
                raise RuntimeError(f"Compiled an unselected scene implementation: {file}")
        if "/servers/" in file:
            owner = "graph" if "/graph/" in file else "rendering"
            if path.name == "texture_blit.cpp":
                owner = "blit"
            if owner not in features:
                raise RuntimeError(f"Compiled an unselected server implementation: {file}")
        if "/ui/" in file and "editor_gui" not in features:
            raise RuntimeError(f"Compiled an unselected UI implementation: {file}")
        if path.name == "editor_gui_shader_material.cpp" and "shader_inspector" not in features:
            raise RuntimeError(f"Compiled an unselected shader Inspector: {file}")


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
    errors = [line for line in output.splitlines() if "[error]" in line]
    if target == "module-research-extension":
        expected = {"Research extension: test.discrete: invalid discrete parameter: quality": 1,
                    "Research extension: Duplicate pass type": 1, "Research extension: Register one project editor": 2,
                    "Research extension: Pass registration has ended": 1, "Research extension: Resource belongs": 1,
                    "Research extension: Expired graph frame": 1, "gain.slang: Load shader module": 1,
                    "[Shader] Pipeline builder returned null": 1, "Research extension: Unknown project shader handle": 1}
        if len(errors) != sum(expected.values()) or any(
                sum(token in error for error in errors) != count for token, count in expected.items()):
            raise RuntimeError(f"{target} emitted unexpected recovery diagnostics: {output}")
        if "Validation Error" in output:
            raise RuntimeError(f"{target} emitted GPU validation errors: {output}")
        return
    expected = 0
    if target == "module-scripting":
        expected = len(features & {"script_native", "script_lua", "script_csharp"})
    if len(errors) != expected or any("[App] [error] Reload " not in line for line in errors):
        raise RuntimeError(f"{target} emitted unexpected diagnostics: {output}")
    if "Validation Error" in output:
        raise RuntimeError(f"{target} emitted GPU validation errors: {output}")


def main():
    temporary = ROOT / "build/.tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix="modules-", dir=temporary))
    env = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "VULTRA_MODULE_TEST_WINDOW"):
        env.pop(name, None)
    cases = {
        "foundation": "foundation",
        "gpu": None,  # The embedded-project default must really be GPU-only.
        "graph": "graph",
        "graph_json": "graph_json",
        "image_io": "image_io",
        "desktop": "desktop",
        "shader": "shader",
        "shader_graph": "shader,graph",
        "shader_graph_json": "shader,graph_json",
        "asset_source": "asset_source",
        "project": "project",
        "game_shader": "game_shader",
        "editor_gui": "editor_gui,image_io",
        "editor_gui_window": "editor_gui,window,image_io",
        "vgui": "vgui",
        "vgui_window": "vgui,window",
        "material_values": "material_values",
        "asset_values": "asset_values",
        "camera": "camera",
        "texture_import": "texture_import",
        "asset_import": "asset_import",
        "world": "world",
        "rendering": "rendering",
        "scene_rendering": "scene_rendering",
        "asset_export": "asset_export",
        "api_scene": "api_scene",
        "api_ui": "api_ui",
        "api_scene_ui": "api_scene,api_ui,image_io",
        "api_host": "api_host",
        "native_plugin": "native_plugin",
        "script_host": "script_host",
        "script_native": "script_native",
        "script_lua": "script_lua",
        "script_csharp": "script_csharp",
        "script_all_cpu": "script_native,script_lua,script_csharp",
        "script_all_ui": "script_native,script_lua,script_csharp,api_ui",
        "native_sdk": "native_sdk",
        "interactive": "interactive",
        "engine": "engine",
        "research_quality": "research_quality",
        "research_camera": "research_camera",
        "research_project": "research_project",
        "research_graph": "research_graph",
        "research_reference": "research_reference",
        "research_reference_correctness": "research_reference,asset_import,image_metrics",
        "research_experiment": "research_experiment",
        "research_api": "research_api",
        "research_api_cpu_host": "research_api,research_project,api_host,image_metrics",
        "research": "research",
    }
    if len(sys.argv) > 1:
        requested = sys.argv[1:]
        unknown = set(requested) - cases.keys()
        if unknown:
            raise ValueError(f"Unknown module cases: {sorted(unknown)}")
        cases = {name: cases[name] for name in requested}
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
            expected_targets.add("module-assets")
        if "editor_gui" in features and "image_io" in features:
            expected_targets.add("module-gui")
        if "vgui" in features:
            expected_targets.update(("vultra-vgui", "module-vgui"))
        if "game_shader" in features:
            expected_targets.add("module-game-shader")
        if "world" in features:
            expected_targets.update(("module-world-ecs", "module-world-execution"))
        if "rendering" in features:
            expected_targets.update(("openpbr", "module-rendering"))
        if "texture_import" in features:
            expected_targets.add("bc7enc")
        if "asset_import" in features:
            expected_targets.add("module-import")
        if "material_values" in features:
            expected_targets.add("module-material")
        if "api_scene" in features or "api_ui" in features:
            expected_targets.add("module-abi")
        if "api_host" in features:
            expected_targets.add("module-plugin-session")
        if "native_sdk" in features:
            expected_targets.add("vultra-native-sdk")
        if features & RESEARCH_SOURCES.keys():
            expected_targets.add("vultra-research-extension")
        if "research_host" in features:
            expected_targets.add("vultra-research-scripting")
        if "research_quality" in features:
            expected_targets.add("flip")
        if {"research_quality", "research_camera", "research_project", "research_graph"} & features:
            expected_targets.add("module-research")
        if {"research_reference", "asset_import", "image_metrics"} <= features:
            expected_targets.add("module-reference")
        if "research_experiment" in features:
            expected_targets.add("module-experiment")
        if {"research_api", "research_project", "api_host", "image_metrics"} <= features:
            expected_targets.add("module-research-extension")
        if "script_host" in features:
            expected_targets.update(("vultra-scripting", "module-scripting"))
        if "script_native" in features:
            expected_targets.update(("module-native-code", "module-native-replacement"))
        if "script_csharp" in features:
            expected_targets.add("module-managed-assets")
        if set(manifest["targets"]) != expected_targets:
            raise RuntimeError(f"{name} registered unrelated targets: {manifest['targets']}")
        expected_packages = {"argparse", "spdlog"} | {
            package for package, owners in PACKAGE_FEATURES.items() if owners & features
        }
        if packages != expected_packages:
            raise RuntimeError(f"{name} package mismatch: expected {expected_packages}; configured {packages}")
        scripting_sources = {Path(file).name for file in manifest["scripting_sources"]}
        expected_scripting = {file for feature, file in (
            ("script_host", "script_host.cpp"), ("script_native", "native_script.cpp"),
            ("script_lua", "lua_script.cpp"), ("script_csharp", "dotnet_script.cpp")) if feature in features}
        if scripting_sources != expected_scripting:
            raise RuntimeError(f"{name} language-source mismatch: {scripting_sources}; expected {expected_scripting}")
        research_sources = {Path(file).name for file in manifest["research_sources"] + manifest["research_host_sources"]}
        expected_research = {file for feature, files in RESEARCH_SOURCES.items() if feature in features for file in files}
        if research_sources != expected_research:
            raise RuntimeError(f"{name} research-source mismatch: {research_sources}; expected {expected_research}")
        if any("extensions" in Path(file).parts for file in manifest["sources"]):
            raise RuntimeError(f"{name} compiled research implementation inside the core library")
        source_paths = [Path(file).as_posix() for file in manifest["sources"] + manifest["scripting_sources"] +
                        manifest["research_sources"] + manifest["research_host_sources"]]
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
            if target in ("module-assets", "module-vgui", "module-import", "module-scripting", "module-research"):
                arguments.append(str(directory / target))
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
