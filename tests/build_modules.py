"""Build the vultra library once per module, each selected alone with only its declared dependencies.

Every case has its own configuration and build directory under build/.tmp/modules/<module>; the main
build/ configuration is not touched. Platform, architecture, mode and the VRI checkout are read from the
main configuration. Pass module names to build a subset.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "build/.tmp/modules"
MODULES = ["core", "platform", "gpu", "shader", "graph", "assets", "render", "ui", "app", "xr"]
MAIN_OPTIONS = ["plat", "arch", "mode", "libvultra_vri_dir"]


def main_configuration():
    script = "import('core.project.config') config.load() " + " ".join(
        f"print('{name}=' .. tostring(config.get('{name}') or ''))" for name in MAIN_OPTIONS)
    result = subprocess.run(["xmake", "lua", "-c", script], cwd=ROOT, capture_output=True, text=True, check=True)
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    if not values.get("plat"):
        raise RuntimeError("Configure the main project first; its platform selects the module builds")
    return values


def build(module, main):
    directory = OUTPUT / module
    directory.mkdir(parents=True, exist_ok=True)
    env = os.environ | {"XMAKE_CONFIGDIR": str(directory)}
    configure = ["xmake", "f", "-y", "-p", main["plat"], "-a", main["arch"], "-m", main["mode"],
                 "-o", str(directory / "build"), "--libvultra_features=" + module,
                 "--libvultra_build_tests=n", "--libvultra_build_examples=n"]
    if main["libvultra_vri_dir"]:
        configure.append("--libvultra_vri_dir=" + main["libvultra_vri_dir"])
    with (directory / "build.log").open("w", encoding="utf-8") as log:
        for command in (configure, ["xmake", "build", "-y", "vultra"]):
            if subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT).returncode != 0:
                return False
    return True


def main():
    modules = sys.argv[1:] or MODULES
    unknown = set(modules) - set(MODULES)
    if unknown:
        raise ValueError(f"Unknown modules: {sorted(unknown)}")
    configuration = main_configuration()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # The root project regenerates compile_commands.json for whichever configuration built last.
    commands = ROOT / "compile_commands.json"
    saved = OUTPUT / "compile_commands.json"
    if commands.is_file():
        shutil.copy2(commands, saved)
    failed = []
    try:
        for module in modules:
            passed = build(module, configuration)
            print(f"{module}: {'pass' if passed else 'FAIL'} ({OUTPUT / module / 'build.log'})", flush=True)
            if not passed:
                failed.append(module)
    finally:
        if saved.is_file():
            shutil.copy2(saved, commands)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
