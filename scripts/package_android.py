"""Package a built Vultra shared library and embedded VPK with GameActivity (no xmake required)."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile


ANDROID_NAMESPACE = "http://schemas.android.com/apk/res/android"


def load_manifest(path, library_name):
    document = ET.parse(path)
    attribute = "{" + ANDROID_NAMESPACE + "}"
    activity = document.find("application/activity")
    if activity is None or activity.get(attribute + "name") != "com.google.androidgamesdk.GameActivity":
        raise ValueError("manifest must launch GameActivity")
    library = next((item for item in activity.findall("meta-data")
                    if item.get(attribute + "name") == "android.app.lib_name"), None)
    if library is None:
        raise ValueError("manifest must declare android.app.lib_name")
    library.set(attribute + "value", library_name)
    document.getroot().attrib.pop("package", None)
    ET.register_namespace("android", ANDROID_NAMESPACE)
    return document


def verify_apk(apk, build_tools):
    subprocess.run([build_tools / "apksigner", "verify", "--verbose", apk], check=True)
    subprocess.run([build_tools / "zipalign", "-P", "16", "-c", "4", apk], check=True)
    with zipfile.ZipFile(apk) as archive:
        if archive.getinfo("assets/project.vpk").compress_type != zipfile.ZIP_STORED:
            raise RuntimeError("APK compressed its embedded VPK")
        if "classes.dex" not in archive.namelist():
            raise RuntimeError("GameActivity Java classes are missing")
        if any(name.endswith("libc++_shared.so") for name in archive.namelist()):
            raise RuntimeError("Base Android application must statically link the C++ runtime")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("vpk", type=Path)
    parser.add_argument("output", type=Path, help="Fresh directory for a debug-signed APK and Gradle project")
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--gradle", type=Path, required=True)
    parser.add_argument("--abi", choices=["arm64-v8a", "x86_64"], required=True)
    parser.add_argument("--application-id", default="org.vultra.example")
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    library = args.library.resolve()
    if not library.name.startswith("lib") or library.suffix != ".so" or len(library.stem) <= 3:
        parser.error("library must be a built libNAME.so")
    manifest = args.manifest or root / "examples/common/android_manifest.xml"
    for path in (library, args.vpk, manifest, args.gradle):
        if not path.is_file():
            parser.error("required file is missing: " + str(path))
    build_tools = args.sdk.resolve() / "build-tools/35.0.0"
    for name in ("apksigner", "zipalign"):
        if not (build_tools / name).is_file():
            parser.error("Android build tool is missing: " + str(build_tools / name))
    name = library.name[3:-3]
    try:
        document = load_manifest(manifest, name)
    except (ValueError, ET.ParseError) as error:
        parser.error(str(error))

    # Validate inputs before creating the fresh output directory; failed builds remain available for diagnosis.
    output = args.output.resolve()
    output.mkdir()
    project = output / "project"
    shutil.copytree(root / "source/platform/android", project)
    document.write(project / "AndroidManifest.xml", encoding="utf-8", xml_declaration=True)
    destination = project / "jniLibs" / args.abi
    destination.mkdir(parents=True)
    shutil.copyfile(library, destination / library.name)
    assets = project / "assets"
    assets.mkdir()
    shutil.copyfile(args.vpk, assets / "project.vpk")
    (project / "local.properties").write_text("sdk.dir=" + str(args.sdk.resolve()) + "\n", encoding="utf-8")
    environment = os.environ.copy()
    # The debug key belongs to this package; callers can isolate Gradle downloads with GRADLE_USER_HOME.
    subprocess.run(["keytool", "-genkeypair", "-keystore", str(project / "debug.keystore"), "-storepass", "android",
                    "-keypass", "android", "-alias", "test", "-keyalg", "RSA", "-validity", "30",
                    "-dname", "CN=Vultra Android Example", "-noprompt"], check=True, env=environment)
    subprocess.run([str(args.gradle.resolve()), "--no-daemon", "--project-dir", str(project),
                    "-PvultraApplicationId=" + args.application_id, "assembleDebug"], check=True, env=environment)
    apks = list((project / "build/outputs/apk/debug").glob("*.apk"))
    if len(apks) != 1:
        raise RuntimeError("Expected exactly one debug APK")
    apk = output / (name + ".apk")
    shutil.copyfile(apks[0], apk)
    verify_apk(apk, build_tools)
    print(apk)


if __name__ == "__main__":
    main()
