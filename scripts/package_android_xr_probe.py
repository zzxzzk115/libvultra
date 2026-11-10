#!/usr/bin/env python3
"""Package the built NativeActivity OpenXR probe as a debug-signed APK."""

import argparse
import pathlib
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=pathlib.Path,
                        help="Built arm64-v8a NativeActivity shared library")
    parser.add_argument("--manifest", type=pathlib.Path,
                        help="NativeActivity manifest (defaults to the libvultra probe manifest)")
    parser.add_argument("--asset-root", type=pathlib.Path, help="Directory to package under assets/")
    parser.add_argument("--asset", action="append", default=[], metavar="APK_PATH=FILE",
                        help="Add a file under assets/, e.g. passes/image_difference.vshaderc=path")
    parser.add_argument("--sdk", required=True, type=pathlib.Path,
                        help="Android SDK root")
    parser.add_argument("--build-tools", required=True,
                        help="Installed Android build-tools version")
    parser.add_argument("--platform", default="android-36", help="Installed Android SDK platform")
    parser.add_argument("--keystore", default=pathlib.Path.home() / ".android/debug.keystore", type=pathlib.Path,
                        help="Android debug keystore, with the standard android password")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    library = args.library.resolve(strict=True)
    if not library.name.startswith("lib") or library.suffix != ".so":
        parser.error("--library must be a lib*.so shared library")
    sdk = args.sdk.resolve(strict=True)
    tools = sdk / "build-tools" / args.build_tools
    jar = sdk / "platforms" / args.platform / "android.jar"
    manifest = args.manifest or pathlib.Path(__file__).resolve().parent.parent / "tests/platform/xr_android_manifest.xml"
    output = args.output.resolve()
    assets = []
    if args.asset_root is not None:
        root = args.asset_root.resolve()
        if not root.is_dir():
            parser.error("--asset-root must be a directory")
        if output.is_relative_to(root):
            parser.error("--output must be outside --asset-root")
        for path in sorted(root.rglob("*")):
            if path.is_symlink():
                parser.error(f"--asset-root contains a symbolic link: {path}")
            if path.is_file():
                assets.append((path.relative_to(root).as_posix(), path))
        if not assets:
            parser.error("--asset-root contains no files")
    for item in args.asset:
        name, separator, source = item.partition("=")
        path = pathlib.PurePosixPath(name)
        if not separator or not source or path.is_absolute() or not path.parts or ".." in path.parts:
            parser.error("--asset requires a safe relative APK_PATH=FILE")
        assets.append((path.as_posix(), pathlib.Path(source).resolve(strict=True)))
    if len({name for name, _ in assets}) != len(assets):
        parser.error("duplicate --asset destination")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vultra-xr-apk-") as folder:
        root = pathlib.Path(folder)
        raw, aligned = root / "unsigned.apk", root / "aligned.apk"
        subprocess.run([str(tools / "aapt2"), "link", "-I", str(jar), "--manifest", str(manifest),
                        "-o", str(raw)], check=True)
        with zipfile.ZipFile(raw, "a", compression=zipfile.ZIP_STORED) as archive:
            archive.write(library, "lib/arm64-v8a/" + library.name)
            for name, source in assets:
                archive.write(source, "assets/" + name)
        subprocess.run([str(tools / "zipalign"), "-f", "-p", "4", str(raw), str(aligned)], check=True)
        subprocess.run([str(tools / "apksigner"), "sign", "--ks", str(args.keystore), "--ks-pass", "pass:android",
                        "--key-pass", "pass:android", "--out", str(output), str(aligned)], check=True)
        subprocess.run([str(tools / "apksigner"), "verify", str(output)], check=True)
    print(output)


if __name__ == "__main__":
    main()
