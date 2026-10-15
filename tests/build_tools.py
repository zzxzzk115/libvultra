"""Verify standalone tool/player selections and run their windowless delivery operations."""

import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from build_modules import PACKAGE_FEATURES, ROOT, run, verify_dependencies, verify_sources


REPORT = """function main(output)
    import("core.project.project")
    import("core.project.config")
    import("core.base.json")
    assert(config.load())
    project.load_targets()
    local targets = {}
    for name, target in pairs(project.targets()) do
        local sources = target:sourcefiles()
        json.mark_as_array(sources)
        local dependencies = {}
        for _, dependency in ipairs(target:orderdeps()) do
            table.insert(dependencies, dependency:name())
        end
        json.mark_as_array(dependencies)
        targets[name] = {kind = target:kind(), file = target:targetfile(), sources = sources, deps = dependencies}
    end
    local packages = {}
    for name in pairs(project.required_packages()) do
        table.insert(packages, name)
    end
    json.mark_as_array(packages)
    io.writefile(output, json.encode({features = project.target("vultra"):values("vultra.features"),
                                     packages = packages, targets = targets}))
end
"""
CONSUMERS = {
    "tool_import": "vultra-import", "tool_shader": "vultra-shader", "tool_pack": "vultra-pack",
    "tool_sdk": "vultra-sdk", "tool_batch": "vultra-batch", "tool_python": "vultra-research",
    "runtime": "vultra-runtime", "editor": "vultra-app", "builtin_pack": "vultra-builtin-pack",
}


def main():
    evidence = Path(tempfile.mkdtemp(prefix="tools-", dir=ROOT / "build/.tmp"))
    report_script = evidence / "report.lua"
    report_script.write_text(REPORT)
    env = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY"):
        env.pop(name, None)
    cases = {
        "tool_import": "tool_import", "tool_shader": "tool_shader", "tool_pack": "tool_pack",
        "tool_sdk": "tool_sdk", "tool_batch": "tool_batch", "tool_python": "tool_python",
        "runtime": "runtime", "runtime_ui": "runtime,api_ui", "editor": "editor",
    }
    if len(sys.argv) > 1:
        unknown = set(sys.argv[1:]) - cases.keys()
        if unknown:
            raise ValueError(f"Unknown tool cases: {sorted(unknown)}")
        cases = {name: cases[name] for name in sys.argv[1:]}
    for name, selection in cases.items():
        directory = evidence / name
        directory.mkdir()
        build = directory / "build"
        case_env = env | {"XMAKE_CONFIGDIR": str(directory)}
        run(["xmake", "f", "-y", "-c", "-m", "release", "-o", str(build),
             "--libvultra_features=" + selection, "--libvultra_build_examples=n", "--libvultra_build_tests=n",
             "--libvultra_window_backend=sdl3", "--libvultra_with_openxr=n"],
            directory / "configure.log", env=case_env, cwd=ROOT)
        run(["xmake", "build", "-a", "-y"], directory / "build.log", env=case_env, cwd=ROOT)
        shutil.copy2(ROOT / "compile_commands.json", directory / "compile_commands.json")
        report_file = directory / "report.json"
        run(["xmake", "lua", str(report_script), str(report_file)], directory / "report.log", env=case_env, cwd=ROOT)
        manifest = json.loads(report_file.read_text())
        features = set(manifest["features"])
        packages = set(manifest["packages"])
        expected_packages = {"argparse", "spdlog"} | {
            package for package, owners in PACKAGE_FEATURES.items() if features & owners}
        if packages != expected_packages:
            raise RuntimeError(f"{name} package mismatch: {packages}; expected {expected_packages}")
        targets = manifest["targets"]
        consumers = set(targets) & set(CONSUMERS.values())
        expected_consumers = {target for feature, target in CONSUMERS.items() if feature in features}
        if consumers != expected_consumers:
            raise RuntimeError(f"{name} standalone target mismatch: {consumers}; expected {expected_consumers}")
        if any(target.startswith(("example-", "test-")) for target in targets):
            raise RuntimeError(f"{name} registered the repository example/test collections")
        sources = [str((ROOT / file).resolve()) for file in targets["vultra"]["sources"]]
        verify_sources(features, sources)
        if any("extensions" in Path(file).parts for file in sources):
            raise RuntimeError(f"{name} compiled research code into the core")
        if "runtime" in features:
            dependencies = targets["vultra-runtime"]["deps"]
            if any("research" in dep or dep == "vultra-app" for dep in dependencies):
                raise RuntimeError(f"Player acquired a research/editor dependency: {dependencies}")
            if "research" in features or "editor" in features or "tools" in features:
                raise RuntimeError("Player selected the research/editor/tool convenience groups")
        if "script_csharp" not in features and "vultra-managed-host" in targets:
            raise RuntimeError(f"{name} registered an unselected managed build")
        for target, description in targets.items():
            if description["kind"] not in ("binary", "shared"):
                continue
            executable = (ROOT / description["file"]).resolve()
            verify_dependencies(executable, features, directory / (target + "-dynamic.log"), case_env)
            if description["kind"] == "binary":
                # Copy the built CLI outside its build tree and remove xmake from the process PATH.
                delivered = directory / executable.name
                shutil.copy2(executable, delivered)
                delivered_env = case_env | {"PATH": str(directory)}
                run([str(delivered), "--help"], directory / (target + "-help.log"), env=delivered_env, cwd=directory)
        if name == "tool_import":
            binary = directory / Path(targets["vultra-import"]["file"]).name
            arguments = [str(binary), str(ROOT / "resources/models/CornellBox/CornellBox-Original.obj"),
                         "--cache-dir", str(directory / "cache")]
            for phase in ("cold", "warm"):
                run(arguments, directory / (phase + ".log"), env=case_env | {"PATH": str(directory)}, cwd=directory)
            if "Asset cache is current" not in (directory / "warm.log").read_text():
                raise RuntimeError("Delivered importer did not reuse its asset cache")
        if name == "tool_shader":
            binary = directory / Path(targets["vultra-shader"]["file"]).name
            for label, source in (("native", "examples/research/shaders/triangle.slang"),
                                  ("game", "examples/shader/painted_metal.vshader")):
                artifact = directory / (label + ".vshaderc")
                run([str(binary), str(ROOT / source), "--output", str(artifact), "--include",
                     str(ROOT / "builtin/shaders"), "--include", str(ROOT / "external"), "--include",
                     str(ROOT / "examples/common")],
                    directory / (label + ".log"), env=case_env | {"PATH": str(directory)}, cwd=directory)
                if not artifact.is_file() or artifact.stat().st_size == 0:
                    raise RuntimeError("Delivered shader tool did not cook an artifact")
        if name == "tool_pack":
            binary = directory / Path(targets["vultra-pack"]["file"]).name
            package = directory / "research.vpk"
            delivered_env = case_env | {"PATH": str(directory)}
            run([str(binary), str(ROOT / "resources/research.vproject"), str(package), "--engine", str(ROOT)],
                directory / "pack.log", env=delivered_env, cwd=directory)
            embedded = directory / "embedded-packer"
            run([str(binary), "--embed", str(binary), str(package), str(embedded)],
                directory / "embed.log", env=delivered_env, cwd=directory)
            run([str(embedded), "--help"], directory / "embedded.log", env=delivered_env, cwd=directory)
        if name == "tool_sdk":
            source = directory / "sdk.c"
            source.write_text('#include <vultra/api/native_plugin.h>\n'
                              '#include <vultra/research/api/research_host.h>\n'
                              '#include <vultra/research/api/research_api.h>\n'
                              'int main(void) { VultraHostApi host = {0}; return host.ui != 0; }\n')
            binary = directory / "sdk-probe"
            run(["cc", "-std=c11", "-Werror", "-I", str(ROOT / "build/sdk/include"), str(source), "-o", str(binary)],
                directory / "sdk-compile.log", env=case_env, cwd=directory)
            run([str(binary)], directory / "sdk-run.log", env=case_env | {"PATH": str(directory)}, cwd=directory)
        if name == "runtime":
            binary = directory / Path(targets["vultra-runtime"]["file"]).name
            output = run([str(binary), "--debug-ui"], directory / "missing-ui.log", succeeds=False,
                         env=case_env | {"PATH": str(directory)}, cwd=directory)
            if "requires the api_ui feature" not in output:
                raise RuntimeError("Player did not reject unavailable UI before creating a window")
        print(f"{name}: selected sources/packages/consumers and delivered CLI checks passed", flush=True)
    print(f"Tool delivery evidence: {evidence}", flush=True)


if __name__ == "__main__":
    main()
