"""Package the built platform probe without xmake; SDK tools and a JDK are required."""

import argparse
from pathlib import Path
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("abi", choices=["arm64-v8a", "x86_64"])
    parser.add_argument("library", type=Path)
    parser.add_argument("vpk", type=Path)
    parser.add_argument("output", type=Path, help="Fresh output directory; creates a test-only signing key")
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("android_manifest.xml"))
    parser.add_argument("--library-name", default="platform-render")
    args = parser.parse_args()
    tools = args.sdk.resolve() / "build-tools/35.0.0"
    output = args.output.resolve()
    output.mkdir()
    assets = output / "assets"
    assets.mkdir()
    shutil.copyfile(args.vpk, assets / "project.vpk")
    manifest = args.manifest.resolve()
    base = output / "base.apk"
    subprocess.run([tools / "aapt2", "link", "-I", args.sdk.resolve() / "platforms/android-35/android.jar",
                    "--manifest", manifest, "-A", assets, "-0", "vpk", "-o", base], check=True)
    with zipfile.ZipFile(base, "a", compression=zipfile.ZIP_STORED) as archive:
        archive.write(args.library, "lib/" + args.abi + "/lib" + args.library_name + ".so")
        if archive.getinfo("assets/project.vpk").compress_type != zipfile.ZIP_STORED:
            raise RuntimeError("APK compressed the VPK; direct mapping requires stored bytes")
    aligned = output / "aligned.apk"
    subprocess.run([tools / "zipalign", "-P", "16", "-f", "4", base, aligned], check=True)
    key = output / "test.jks"
    subprocess.run(["keytool", "-genkeypair", "-keystore", key, "-storepass", "android", "-keypass", "android",
                    "-alias", "test", "-keyalg", "RSA", "-validity", "30", "-dname", "CN=Vultra Platform Test",
                    "-noprompt"], check=True)
    apk = output / "platform-probe.apk"
    subprocess.run([tools / "apksigner", "sign", "--ks", key, "--ks-pass", "pass:android", "--out", apk,
                    aligned], check=True)
    subprocess.run([tools / "apksigner", "verify", "--verbose", apk], check=True)
    subprocess.run([tools / "zipalign", "-P", "16", "-c", "4", apk], check=True)
    print(apk)


if __name__ == "__main__":
    main()
