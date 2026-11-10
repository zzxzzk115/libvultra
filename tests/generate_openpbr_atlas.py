"""Generate the frozen 32-wide FP32 atlas reference from the pinned OpenPBR data."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def contents(path):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", path.read_text(), flags=re.DOTALL)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    data = root / "external/openpbr/impl/data"
    tables = [
        ("ideal_dielectric_energy_complement", 32, 32),
        ("ideal_dielectric_avg_energy_complement", 32, 1),
        ("ideal_dielectric_reflection_ratio", 32, 1),
        ("opaque_dielectric_energy_complement", 32, 32),
        ("opaque_dielectric_avg_energy_complement", 32, 1),
        ("ideal_metal_energy_complement", 32, 1),
        ("ideal_metal_avg_energy_complement", 1, 1),
    ]
    pixels = [struct.pack("<4f", 0, 0, 0, 0)] * 32
    sources = []
    for index, (name, height, depth) in enumerate(tables):
        path = data / ("openpbr_" + name + "_data.h")
        values = [int(v) for v in re.findall(r"\b\d+\b", contents(path))]
        assert len(values) == 32 * height * depth and all(0 <= v <= 65535 for v in values)
        pixels[index] = struct.pack("<4f", len(pixels), 32, height, depth)
        pixels.extend(struct.pack("<4f", value / 65535.0, 0, 0, 0) for value in values)
        sources.append(path)
    path = data / "openpbr_ltc_data.h"
    triples = re.findall(r"vec3\(([^)]+)\)", contents(path))
    assert len(triples) == 1024
    pixels[7] = struct.pack("<4f", len(pixels), 32, 32, 1)
    for triple in triples:
        values = [float(v) for v in triple.split(",")]
        assert len(values) == 3
        pixels.append(struct.pack("<4f", *values, 0))
    sources.append(path)
    payload = b"".join(pixels)
    assert len(payload) == 32 * 2210 * 16
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as output:
        output.write(payload)
    manifest = {
        "format": "little-endian RGBA32F, 32x2210, metadata row and upstream table order",
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "sources": [
            {
                "path": str(path.relative_to(root)),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            for path in sources
        ],
    }
    args.output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(manifest["sha256"])


if __name__ == "__main__":
    main()
