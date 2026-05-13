#!/usr/bin/env python3
"""Train or estimate per-Gaussian importance for libvultra Ordered CLOD.

The official CLOD-3DGS training code teaches the model to survive random
prefixes: after normal 3DGS training, each iteration randomly renders only the
first N splats. This tool adapts that idea to libvultra's renderer contract,
where training and rendering are intentionally separate and the renderer only
consumes one scalar importance/order per Gaussian.

Three modes are provided:

* heuristic: no CUDA dependency. Produces a strong first ordering from opacity,
  projected footprint, color energy and multi-view coverage.
* gsplat: optional differentiable score training. Gaussian parameters stay
  frozen; only one score per splat is optimized under random LOD budgets.
* clod: CLOD-like prefix finetuning. The order is fixed, each step renders a
  random prefix, and opacity/SH are finetuned against GT plus a full-model
  teacher so low budgets learn to preserve the full model.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np
from PIL import Image


PLY_TO_NUMPY: dict[str, str] = {
    "char": "i1",
    "int8": "i1",
    "uchar": "u1",
    "uint8": "u1",
    "short": "i2",
    "int16": "i2",
    "ushort": "u2",
    "uint16": "u2",
    "int": "i4",
    "int32": "i4",
    "uint": "u4",
    "uint32": "u4",
    "float": "f4",
    "float32": "f4",
    "double": "f8",
    "float64": "f8",
}

NUMPY_TO_PLY: dict[str, str] = {
    "i1": "char",
    "u1": "uchar",
    "i2": "short",
    "u2": "ushort",
    "i4": "int",
    "u4": "uint",
    "f4": "float",
    "f8": "double",
}

SH_C0 = 0.28209479177387814


@dataclass
class PlyData:
    path: Path
    fmt: str
    vertex_count: int
    properties: list[tuple[str, str]]
    vertices: np.ndarray


@dataclass
class Camera:
    name: str
    width: int
    height: int
    fx: float
    fy: float
    cx: float
    cy: float
    c2w: np.ndarray


def sigmoid_np(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, -60.0, 60.0)
    return 1.0 / (1.0 + np.exp(-x))


def normalize01(values: np.ndarray) -> np.ndarray:
    finite = np.isfinite(values)
    if not finite.any():
        return np.zeros_like(values, dtype=np.float32)
    lo, hi = np.percentile(values[finite], [1.0, 99.0])
    if hi <= lo + 1e-12:
        return np.zeros_like(values, dtype=np.float32)
    out = (values - lo) / (hi - lo)
    return np.clip(out, 0.0, 1.0).astype(np.float32)


def dtype_from_properties(properties: list[tuple[str, str]], endian: str) -> np.dtype:
    fields: list[tuple[str, str]] = []
    for name, ply_type in properties:
        if ply_type not in PLY_TO_NUMPY:
            raise ValueError(f"Unsupported PLY property type '{ply_type}' for '{name}'")
        fields.append((name, endian + PLY_TO_NUMPY[ply_type]))
    return np.dtype(fields)


def read_ply(path: Path) -> PlyData:
    with path.open("rb") as f:
        first = f.readline().decode("ascii", errors="strict").strip()
        if first != "ply":
            raise ValueError(f"{path} is not a PLY file")

        fmt = ""
        vertex_count: int | None = None
        properties: list[tuple[str, str]] = []
        current_element: str | None = None

        while True:
            raw = f.readline()
            if not raw:
                raise ValueError(f"{path} has no end_header")
            line = raw.decode("ascii", errors="strict").strip()
            if line == "end_header":
                data_offset = f.tell()
                break
            if not line or line.startswith("comment"):
                continue
            parts = line.split()
            if parts[0] == "format":
                fmt = parts[1]
            elif parts[0] == "element":
                current_element = parts[1]
                if current_element == "vertex":
                    vertex_count = int(parts[2])
                elif int(parts[2]) != 0:
                    raise ValueError(
                        "Only vertex-only Gaussian PLY files are supported by this tool"
                    )
            elif parts[0] == "property" and current_element == "vertex":
                if parts[1] == "list":
                    raise ValueError("List PLY properties are not supported")
                properties.append((parts[2], parts[1]))

        if fmt not in {"ascii", "binary_little_endian", "binary_big_endian"}:
            raise ValueError(f"Unsupported PLY format '{fmt}'")
        if vertex_count is None:
            raise ValueError(f"{path} has no vertex element")

        if fmt == "ascii":
            rows: list[tuple[Any, ...]] = []
            converters = [np.dtype(PLY_TO_NUMPY[t]).type for _, t in properties]
            for _ in range(vertex_count):
                values = f.readline().decode("ascii", errors="strict").split()
                rows.append(tuple(conv(value) for conv, value in zip(converters, values)))
            dtype = dtype_from_properties(properties, "=")
            vertices = np.array(rows, dtype=dtype)
        else:
            endian = "<" if fmt == "binary_little_endian" else ">"
            dtype = dtype_from_properties(properties, endian)
            f.seek(data_offset)
            vertices = np.frombuffer(f.read(), dtype=dtype, count=vertex_count).copy()

    return PlyData(path=path, fmt=fmt, vertex_count=vertex_count, properties=properties, vertices=vertices)


def write_ply(path: Path, ply: PlyData, vertices: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    properties = [(name, NUMPY_TO_PLY[np.dtype(vertices.dtype[name]).str[-2:]]) for name in vertices.dtype.names or ()]

    fmt = ply.fmt
    if fmt not in {"ascii", "binary_little_endian", "binary_big_endian"}:
        fmt = "binary_little_endian"

    with path.open("wb") as f:
        lines = [
            "ply",
            f"format {fmt} 1.0",
            "comment generated by tools/gaussian_importance/train_importance.py",
            f"element vertex {len(vertices)}",
        ]
        lines.extend(f"property {ply_type} {name}" for name, ply_type in properties)
        lines.append("end_header")
        f.write(("\n".join(lines) + "\n").encode("ascii"))

        if fmt == "ascii":
            for row in vertices:
                f.write((" ".join(str(row[name].item()) for name, _ in properties) + "\n").encode("ascii"))
        else:
            target_endian = "<" if fmt == "binary_little_endian" else ">"
            target_dtype = dtype_from_properties(properties, target_endian)
            f.write(vertices.astype(target_dtype, copy=False).tobytes())


def with_importance(ply: PlyData, importance: np.ndarray, vertices: np.ndarray | None = None) -> np.ndarray:
    importance = np.asarray(importance, dtype=np.float32)
    if importance.shape != (ply.vertex_count,):
        raise ValueError(f"importance has shape {importance.shape}, expected {(ply.vertex_count,)}")

    old = ply.vertices if vertices is None else vertices
    names = list(old.dtype.names or ())
    if "importance" in names:
        out = old.copy()
        out["importance"] = importance
        return out

    dtype_descr = list(old.dtype.descr)
    dtype_descr.append(("importance", "<f4"))
    out = np.empty(old.shape, dtype=np.dtype(dtype_descr))
    for name in names:
        out[name] = old[name]
    out["importance"] = importance
    return out


def get_field(vertices: np.ndarray, name: str, default: float = 0.0) -> np.ndarray:
    if name in (vertices.dtype.names or ()):
        return vertices[name].astype(np.float32)
    return np.full(len(vertices), default, dtype=np.float32)


def ply_with_vertices(ply: PlyData, vertices: np.ndarray) -> PlyData:
    properties: list[tuple[str, str]] = []
    for name in vertices.dtype.names or ():
        key = np.dtype(vertices.dtype[name]).str[-2:]
        properties.append((name, NUMPY_TO_PLY.get(key, "float")))
    return PlyData(
        path=ply.path,
        fmt=ply.fmt,
        vertex_count=len(vertices),
        properties=properties,
        vertices=vertices,
    )


def importance_from_ply_or_heuristic(
    ply: PlyData,
    cameras: list[Camera],
    args: argparse.Namespace,
) -> np.ndarray:
    if args.init_importance == "ply" and "importance" in (ply.vertices.dtype.names or ()):
        return normalize01(ply.vertices["importance"].astype(np.float32))
    return heuristic_importance(ply, cameras, args)


def rank_importance(order: np.ndarray, count: int) -> np.ndarray:
    importance = np.zeros(count, dtype=np.float32)
    if count == 0:
        return importance
    values = np.linspace(1.0, 0.0, count, dtype=np.float32)
    importance[order] = values
    return importance


def gaussian_arrays(ply: PlyData) -> dict[str, np.ndarray]:
    v = ply.vertices
    required = ["x", "y", "z"]
    missing = [name for name in required if name not in (v.dtype.names or ())]
    if missing:
        raise ValueError(f"PLY is missing Gaussian position fields: {missing}")

    positions = np.stack([v["x"], v["y"], v["z"]], axis=-1).astype(np.float32)
    opacity = sigmoid_np(get_field(v, "opacity", 10.0))

    scales = []
    for i in range(3):
        scales.append(np.exp(np.clip(get_field(v, f"scale_{i}", -7.0), -20.0, 20.0)))
    scale = np.stack(scales, axis=-1).astype(np.float32)
    radius = np.maximum(np.max(scale, axis=-1), 1e-6)

    if all(name in (v.dtype.names or ()) for name in ("f_dc_0", "f_dc_1", "f_dc_2")):
        dc = np.stack([v["f_dc_0"], v["f_dc_1"], v["f_dc_2"]], axis=-1).astype(np.float32)
        rgb = np.clip(SH_C0 * dc + 0.5, 0.0, 1.0)
        luma = 0.2126 * rgb[:, 0] + 0.7152 * rgb[:, 1] + 0.0722 * rgb[:, 2]
    else:
        luma = np.ones(len(v), dtype=np.float32)

    return {
        "positions": positions,
        "opacity": opacity.astype(np.float32),
        "radius": radius.astype(np.float32),
        "luma": luma.astype(np.float32),
    }


def camera_matrix_from_json(item: dict[str, Any]) -> Camera:
    width = int(item["width"])
    height = int(item["height"])
    c2w = np.eye(4, dtype=np.float64)
    c2w[:3, :3] = np.asarray(item["rotation"], dtype=np.float64)
    c2w[:3, 3] = np.asarray(item["position"], dtype=np.float64)
    return Camera(
        name=str(item["img_name"]),
        width=width,
        height=height,
        fx=float(item["fx"]),
        fy=float(item["fy"]),
        cx=float(item.get("cx", width * 0.5)),
        cy=float(item.get("cy", height * 0.5)),
        c2w=c2w,
    )


def load_cameras_json(path: Path) -> list[Camera]:
    data = json.loads(path.read_text(encoding="utf-8"))
    return [camera_matrix_from_json(item) for item in data]


def qvec_to_rotmat(qvec: Iterable[float]) -> np.ndarray:
    qw, qx, qy, qz = [float(v) for v in qvec]
    return np.array(
        [
            [1 - 2 * qy * qy - 2 * qz * qz, 2 * qx * qy - 2 * qw * qz, 2 * qz * qx + 2 * qw * qy],
            [2 * qx * qy + 2 * qw * qz, 1 - 2 * qx * qx - 2 * qz * qz, 2 * qy * qz - 2 * qw * qx],
            [2 * qz * qx - 2 * qw * qy, 2 * qy * qz + 2 * qw * qx, 1 - 2 * qx * qx - 2 * qy * qy],
        ],
        dtype=np.float64,
    )


COLMAP_CAMERA_MODELS: dict[int, tuple[str, int]] = {
    0: ("SIMPLE_PINHOLE", 3),
    1: ("PINHOLE", 4),
    2: ("SIMPLE_RADIAL", 4),
    3: ("RADIAL", 5),
    4: ("OPENCV", 8),
    5: ("OPENCV_FISHEYE", 8),
    6: ("FULL_OPENCV", 12),
    7: ("FOV", 5),
    8: ("SIMPLE_RADIAL_FISHEYE", 4),
    9: ("RADIAL_FISHEYE", 5),
    10: ("THIN_PRISM_FISHEYE", 12),
}


def read_next_bytes(f: Any, n: int, fmt: str) -> tuple[Any, ...]:
    data = f.read(n)
    if len(data) != n:
        raise ValueError("Unexpected EOF while reading COLMAP binary")
    return struct.unpack(fmt, data)


def colmap_intrinsics(model_id: int, params: tuple[float, ...]) -> tuple[float, float, float, float]:
    model_name = COLMAP_CAMERA_MODELS[model_id][0]
    if model_name in {"SIMPLE_PINHOLE", "SIMPLE_RADIAL", "RADIAL", "SIMPLE_RADIAL_FISHEYE", "RADIAL_FISHEYE"}:
        f, cx, cy = params[:3]
        return f, f, cx, cy
    fx, fy, cx, cy = params[:4]
    return fx, fy, cx, cy


def load_colmap_cameras(scene_dir: Path) -> list[Camera]:
    sparse = scene_dir / "sparse" / "0"
    cameras_bin = sparse / "cameras.bin"
    images_bin = sparse / "images.bin"
    if not cameras_bin.exists() or not images_bin.exists():
        raise FileNotFoundError(f"Could not find COLMAP cameras/images under {sparse}")

    intrinsics: dict[int, tuple[int, int, float, float, float, float]] = {}
    with cameras_bin.open("rb") as f:
        (num_cameras,) = read_next_bytes(f, 8, "<Q")
        for _ in range(num_cameras):
            camera_id, model_id, width, height = read_next_bytes(f, 24, "<iiQQ")
            if model_id not in COLMAP_CAMERA_MODELS:
                raise ValueError(f"Unsupported COLMAP camera model id {model_id}")
            num_params = COLMAP_CAMERA_MODELS[model_id][1]
            params = read_next_bytes(f, 8 * num_params, "<" + "d" * num_params)
            fx, fy, cx, cy = colmap_intrinsics(model_id, params)
            intrinsics[camera_id] = (int(width), int(height), fx, fy, cx, cy)

    cameras: list[Camera] = []
    with images_bin.open("rb") as f:
        (num_images,) = read_next_bytes(f, 8, "<Q")
        for _ in range(num_images):
            image_id = read_next_bytes(f, 4, "<i")[0]
            qvec = read_next_bytes(f, 32, "<dddd")
            tvec = np.asarray(read_next_bytes(f, 24, "<ddd"), dtype=np.float64)
            camera_id = read_next_bytes(f, 4, "<i")[0]

            name_bytes = bytearray()
            while True:
                ch = f.read(1)
                if ch == b"\x00":
                    break
                name_bytes.extend(ch)
            name = name_bytes.decode("utf-8")

            (num_points2d,) = read_next_bytes(f, 8, "<Q")
            f.seek(int(num_points2d) * 24, os.SEEK_CUR)

            if camera_id not in intrinsics:
                continue
            width, height, fx, fy, cx, cy = intrinsics[camera_id]
            w2c_r = qvec_to_rotmat(qvec)
            c2w = np.eye(4, dtype=np.float64)
            c2w[:3, :3] = w2c_r.T
            c2w[:3, 3] = -w2c_r.T @ tvec
            cameras.append(Camera(name=Path(name).stem, width=width, height=height, fx=fx, fy=fy, cx=cx, cy=cy, c2w=c2w))

    return cameras


def load_cameras(args: argparse.Namespace) -> list[Camera]:
    if args.cameras_json:
        return load_cameras_json(Path(args.cameras_json))
    if args.model and (Path(args.model) / "cameras.json").exists():
        return load_cameras_json(Path(args.model) / "cameras.json")
    if args.scene:
        return load_colmap_cameras(Path(args.scene))
    raise ValueError("Provide --cameras-json, --model with cameras.json, or --scene with COLMAP sparse/0")


def sample_cameras(cameras: list[Camera], max_count: int, seed: int) -> list[Camera]:
    if max_count <= 0 or max_count >= len(cameras):
        return list(cameras)
    rng = random.Random(seed)
    indices = sorted(rng.sample(range(len(cameras)), max_count))
    return [cameras[i] for i in indices]


def heuristic_importance(ply: PlyData, cameras: list[Camera], args: argparse.Namespace) -> np.ndarray:
    arrays = gaussian_arrays(ply)
    positions = arrays["positions"].astype(np.float64)
    opacity = arrays["opacity"].astype(np.float64)
    radius = arrays["radius"].astype(np.float64)
    luma = arrays["luma"].astype(np.float64)

    score = np.zeros(len(positions), dtype=np.float64)
    coverage = np.zeros(len(positions), dtype=np.float64)
    camera_subset = sample_cameras(cameras, args.sample_cameras, args.seed)
    chunk = max(1, args.chunk_size)

    for cam in camera_subset:
        w2c_r = cam.c2w[:3, :3].T
        cam_pos = cam.c2w[:3, 3]
        focal = 0.5 * (cam.fx + cam.fy)
        for start in range(0, len(positions), chunk):
            end = min(start + chunk, len(positions))
            local = (positions[start:end] - cam_pos) @ w2c_r.T
            z = local[:, 2]
            valid_z = z > args.near
            if not np.any(valid_z):
                continue
            inv_z = np.zeros_like(z)
            inv_z[valid_z] = 1.0 / z[valid_z]
            u = cam.fx * local[:, 0] * inv_z + cam.cx
            v = cam.fy * local[:, 1] * inv_z + cam.cy
            margin_x = cam.width * args.frustum_margin
            margin_y = cam.height * args.frustum_margin
            visible = (
                valid_z
                & (u >= -margin_x)
                & (u < cam.width + margin_x)
                & (v >= -margin_y)
                & (v < cam.height + margin_y)
            )
            if not np.any(visible):
                continue
            projected_radius = focal * radius[start:end] * inv_z
            footprint = np.log1p(np.clip(projected_radius * projected_radius, 0.0, args.max_projected_area))
            score[start:end] += visible * footprint
            coverage[start:end] += visible.astype(np.float64)

    if camera_subset:
        score /= float(len(camera_subset))
        coverage /= float(len(camera_subset))

    physical = np.power(opacity, args.opacity_power) * np.power(np.maximum(radius, 1e-9), args.size_power)
    appearance = 0.25 + 0.75 * luma
    combined = score * physical * appearance
    combined += args.coverage_weight * coverage * opacity * appearance

    # Stable tie-breaker keeps deterministic ranks when many scores are equal.
    tie_breaker = np.linspace(1e-8, 0.0, len(combined), dtype=np.float64)
    return normalize01(combined + tie_breaker)


def image_path_for_camera(scene_dir: Path, camera_name: str) -> Path:
    image_dir = scene_dir / "images"
    candidates = [
        image_dir / camera_name,
        image_dir / f"{camera_name}.jpg",
        image_dir / f"{camera_name}.JPG",
        image_dir / f"{camera_name}.png",
        image_dir / f"{camera_name}.PNG",
        image_dir / f"{camera_name}.jpeg",
        image_dir / f"{camera_name}.JPEG",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    matches = list(image_dir.glob(f"{camera_name}.*"))
    if matches:
        return matches[0]
    raise FileNotFoundError(f"No image found for camera '{camera_name}' in {image_dir}")


def torch_splats_from_ply(ply: PlyData, device: str) -> dict[str, Any]:
    import torch

    v = ply.vertices
    names = set(v.dtype.names or ())
    positions = np.stack([v["x"], v["y"], v["z"]], axis=-1).astype(np.float32)
    quats = np.stack([get_field(v, f"rot_{i}", 0.0) for i in range(4)], axis=-1).astype(np.float32)
    scales = np.stack([get_field(v, f"scale_{i}", -7.0) for i in range(3)], axis=-1).astype(np.float32)
    opacities = get_field(v, "opacity", 10.0).astype(np.float32)
    sh0 = np.stack([get_field(v, f"f_dc_{i}", 0.0) for i in range(3)], axis=-1).astype(np.float32)

    rest_fields = sorted(
        (
            int(name[len("f_rest_") :])
            for name in names
            if name.startswith("f_rest_") and name[len("f_rest_") :].isdigit()
        )
    )
    sh_degree: int | None = None
    if rest_fields and len(rest_fields) % 3 == 0:
        rest_count = len(rest_fields) // 3
        shn = np.zeros((len(v), rest_count, 3), dtype=np.float32)
        for coeff in range(rest_count):
            for channel in range(3):
                shn[:, coeff, channel] = get_field(v, f"f_rest_{channel * rest_count + coeff}", 0.0)
        colors = np.concatenate([sh0[:, None, :], shn], axis=1).astype(np.float32)
        coeff_count = colors.shape[1]
        root = int(round(math.sqrt(coeff_count)))
        if root * root == coeff_count:
            sh_degree = max(0, root - 1)
        else:
            sh_degree = 0
            colors = colors[:, :1, :]
    else:
        # Fallback for reduced PLYs that only contain DC color. gsplat accepts
        # direct RGB when colors are [N, 3].
        colors = np.clip(0.5 + SH_C0 * sh0, 0.0, 1.0).astype(np.float32)

    return {
        "means": torch.as_tensor(positions, device=device),
        "quats": torch.as_tensor(quats, device=device),
        "scales": torch.as_tensor(scales, device=device),
        "opacities": torch.as_tensor(opacities, device=device),
        "colors": torch.as_tensor(colors, device=device),
        "sh_degree": sh_degree,
    }


def configure_conda_cuda_home() -> None:
    def prepend_env_paths(name: str, values: list[Path]) -> None:
        current = os.environ.get(name, "")
        existing = [part for part in current.split(os.pathsep) if part]
        prefix = [str(value) for value in values if value.exists()]
        os.environ[name] = os.pathsep.join(prefix + existing)

    os.environ.setdefault("VSLANG", "1033")
    if os.name == "nt":
        os.environ.setdefault("NVCC_PREPEND_FLAGS", "--use-local-env")
    path_parts = [str(Path(sys.prefix) / "Scripts")]

    vs_roots = [
        Path(root) / "Microsoft Visual Studio" / "2022"
        for root in [os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)")]
        if root
    ]
    if os.name == "nt" and not os.environ.get("VSCMD_ARG_TGT_ARCH"):
        vcvars_candidates = []
        for root in vs_roots:
            if root.exists():
                vcvars_candidates.extend(root.glob(r"*\VC\Auxiliary\Build\vcvarsall.bat"))
        for vcvars in sorted(vcvars_candidates, reverse=True):
            result = subprocess.run(
                ["cmd.exe", "/s", "/c", f'call "{vcvars}" x64 >nul && set'],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="ignore",
            )
            if result.returncode != 0:
                continue
            for line in result.stdout.splitlines():
                if "=" not in line:
                    continue
                key, value = line.split("=", 1)
                os.environ[key] = value
            break

    for root in vs_roots:
        if not root.exists():
            continue
        cl_candidates = sorted(root.glob(r"*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe"), reverse=True)
        if cl_candidates:
            cl_path = cl_candidates[0]
            msvc_root = cl_path.parents[3]
            path_parts.append(str(cl_path.parent))
            if os.name == "nt":
                os.environ.setdefault("VSCMD_ARG_TGT_ARCH", "x64")
                os.environ.setdefault("VSCMD_ARG_HOST_ARCH", "x64")
                os.environ.setdefault("VCToolsInstallDir", str(msvc_root) + "\\")
                prepend_env_paths("INCLUDE", [msvc_root / "include"])
                prepend_env_paths("LIB", [msvc_root / "lib" / "x64"])
                prepend_env_paths("LIBPATH", [msvc_root / "lib" / "x64"])

                sdk_root = Path(os.environ.get("WindowsSdkDir", r"C:\Program Files (x86)\Windows Kits\10"))
                include_versions = sorted((sdk_root / "Include").glob("*"), reverse=True) if (sdk_root / "Include").exists() else []
                lib_versions = sorted((sdk_root / "Lib").glob("*"), reverse=True) if (sdk_root / "Lib").exists() else []
                if include_versions:
                    sdk_include = include_versions[0]
                    os.environ.setdefault("WindowsSdkDir", str(sdk_root) + "\\")
                    os.environ.setdefault("WindowsSDKVersion", sdk_include.name + "\\")
                    prepend_env_paths(
                        "INCLUDE",
                        [
                            sdk_include / "ucrt",
                            sdk_include / "shared",
                            sdk_include / "um",
                            sdk_include / "winrt",
                            sdk_include / "cppwinrt",
                        ],
                    )
                if lib_versions:
                    sdk_lib = lib_versions[0]
                    prepend_env_paths("LIB", [sdk_lib / "ucrt" / "x64", sdk_lib / "um" / "x64"])
                    prepend_env_paths("LIBPATH", [sdk_lib / "ucrt" / "x64", sdk_lib / "um" / "x64"])
            break

    if not os.environ.get("TORCH_EXTENSIONS_DIR"):
        repo_root = Path(__file__).resolve().parents[2]
        extensions_dir = repo_root / "build" / "torch_extensions"
        extensions_dir.mkdir(parents=True, exist_ok=True)
        os.environ["TORCH_EXTENSIONS_DIR"] = str(extensions_dir)

    cuda_home = Path(os.environ.get("CUDA_HOME") or os.environ.get("CUDA_PATH") or Path(sys.prefix) / "Library")
    nvcc = cuda_home / "bin" / "nvcc.exe"
    if nvcc.exists():
        os.environ.setdefault("CUDA_HOME", str(cuda_home))
        os.environ.setdefault("CUDA_PATH", str(cuda_home))
        path_parts.append(str(cuda_home / "bin"))
    os.environ["PATH"] = os.pathsep.join(path_parts + [os.environ.get("PATH", "")])


def patch_torch_cpp_extension_for_windows(cpp_extension: Any) -> None:
    cpp_extension.SUBPROCESS_DECODE_ARGS = ("utf-8", "ignore")
    if os.name != "nt" or getattr(cpp_extension, "_vultra_windows_gsplat_patch", False):
        return

    original = cpp_extension._write_ninja_file_to_build_library

    def wrapped_write_ninja_file_to_build_library(
        path: str,
        name: str,
        sources: list[str],
        extra_cflags: list[str],
        extra_cuda_cflags: list[str],
        extra_sycl_cflags: list[str],
        extra_ldflags: list[str],
        extra_include_paths: list[str],
        with_cuda: bool,
        with_sycl: bool,
        is_standalone: bool,
    ) -> None:
        cflags = []
        for flag in extra_cflags or []:
            if flag == "-Wno-attributes":
                continue
            if flag == "-O0":
                cflags.append("/Od")
            elif flag == "-O3":
                cflags.append("/O2")
            else:
                cflags.append(flag)
        ldflags = list(extra_ldflags or [])
        cuda_home = os.environ.get("CUDA_HOME") or os.environ.get("CUDA_PATH")
        if cuda_home:
            cuda_lib = Path(cuda_home) / "lib"
            if (cuda_lib / "cudart.lib").exists():
                lib_flag = f"/LIBPATH:{cuda_lib}"
                if lib_flag not in ldflags:
                    ldflags.append(lib_flag)
        return original(
            path,
            name,
            sources,
            cflags,
            extra_cuda_cflags,
            extra_sycl_cflags,
            ldflags,
            extra_include_paths,
            with_cuda,
            with_sycl,
            is_standalone,
        )

    cpp_extension._write_ninja_file_to_build_library = wrapped_write_ninja_file_to_build_library
    cpp_extension._vultra_windows_gsplat_patch = True


def torch_camera(cam: Camera, device: str, downscale: int) -> tuple[Any, Any, int, int]:
    import torch

    scale = max(1, downscale)
    width = max(1, cam.width // scale)
    height = max(1, cam.height // scale)
    c2w = torch.as_tensor(cam.c2w, dtype=torch.float32, device=device)[None, ...]
    k = torch.tensor(
        [
            [cam.fx / scale, 0.0, cam.cx / scale],
            [0.0, cam.fy / scale, cam.cy / scale],
            [0.0, 0.0, 1.0],
        ],
        dtype=torch.float32,
        device=device,
    )[None, ...]
    return c2w, k, width, height


def load_target_image(path: Path, width: int, height: int, device: str) -> Any:
    import torch

    image = Image.open(path).convert("RGB")
    if image.size != (width, height):
        image = image.resize((width, height), Image.Resampling.LANCZOS)
    arr = np.asarray(image, dtype=np.float32) / 255.0
    return torch.as_tensor(arr, dtype=torch.float32, device=device)


def render_splats(
    rasterization: Any,
    splats: dict[str, Any],
    c2w: Any,
    k: Any,
    width: int,
    height: int,
    *,
    indices: Any | None = None,
    opacities_override: Any | None = None,
    colors_override: Any | None = None,
    sh_degree: int | None = None,
) -> Any:
    import torch

    if indices is None:
        means = splats["means"]
        quats = splats["quats"]
        scales_raw = splats["scales"]
        opacities_raw = splats["opacities"]
        colors = splats["colors"]
    else:
        means = splats["means"][indices]
        quats = splats["quats"][indices]
        scales_raw = splats["scales"][indices]
        opacities_raw = splats["opacities"][indices]
        colors = splats["colors"][indices]

    scales = torch.exp(torch.clamp(scales_raw, -20.0, 20.0))
    opacities = torch.sigmoid(opacities_raw) if opacities_override is None else opacities_override
    colors = colors if colors_override is None else colors_override

    raster_kwargs: dict[str, Any] = {
        "means": means,
        "quats": quats,
        "scales": scales,
        "opacities": opacities,
        "colors": colors,
        "viewmats": torch.linalg.inv(c2w),
        "Ks": k,
        "width": width,
        "height": height,
        "packed": True,
        "rasterize_mode": "classic",
    }
    if colors.ndim == 3:
        degree = splats.get("sh_degree") if sh_degree is None else sh_degree
        if degree is not None:
            raster_kwargs["sh_degree"] = degree

    render_colors, _, _ = rasterization(**raster_kwargs)
    return render_colors[0, ..., :3]


def psnr_from_mse(mse: float) -> float:
    if mse <= 1e-12:
        return 99.0
    return float(-10.0 * math.log10(mse))


def global_ssim(pred: Any, target: Any) -> float:
    import torch

    x = pred.reshape(-1, 3)
    y = target.reshape(-1, 3)
    c1 = 0.01**2
    c2 = 0.03**2
    mux = x.mean(dim=0)
    muy = y.mean(dim=0)
    vx = ((x - mux) ** 2).mean(dim=0)
    vy = ((y - muy) ** 2).mean(dim=0)
    cov = ((x - mux) * (y - muy)).mean(dim=0)
    ssim = ((2.0 * mux * muy + c1) * (2.0 * cov + c2)) / ((mux * mux + muy * muy + c1) * (vx + vy + c2))
    return float(torch.clamp(ssim.mean(), 0.0, 1.0).item())


def load_optional_lpips(device: str) -> Any | None:
    try:
        from torchmetrics.image.lpip import LearnedPerceptualImagePatchSimilarity
    except Exception:
        return None
    metric = LearnedPerceptualImagePatchSimilarity(net_type="alex", normalize=True)
    return metric.to(device).eval()


def effective_sh_degree(args: argparse.Namespace, max_degree: int | None) -> int | None:
    if max_degree is None:
        return None
    if args.sh_degree < 0:
        return max_degree
    return max(0, min(max_degree, args.sh_degree))


def evaluate_lod_curve(
    ply: PlyData,
    cameras: list[Camera],
    args: argparse.Namespace,
    importance: np.ndarray | None = None,
) -> dict[str, Any]:
    configure_conda_cuda_home()
    try:
        import torch
        import torch.utils.cpp_extension as cpp_extension

        patch_torch_cpp_extension_for_windows(cpp_extension)
        from gsplat.rendering import rasterization
    except Exception as exc:  # pragma: no cover - depends on user GPU env.
        raise RuntimeError("LOD evaluation requires CUDA PyTorch with gsplat installed") from exc

    if not args.scene:
        raise ValueError("--scene is required for LOD evaluation because GT images are used")

    device = args.device
    if device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA is not available; use --device cpu only for small debugging runs")

    splats = torch_splats_from_ply(ply, device)
    if importance is None:
        importance = importance_from_ply_or_heuristic(ply, cameras, args)
    order_np = np.argsort(importance)[::-1].copy()
    order_idx = torch.as_tensor(order_np, dtype=torch.long, device=device)

    eval_cameras = sample_cameras(cameras, args.eval_cameras, args.seed + 1009)
    scene_dir = Path(args.scene)
    lpips_metric = load_optional_lpips(device) if args.eval_lpips else None

    report: dict[str, Any] = {
        "ply": str(ply.path),
        "scene": str(scene_dir),
        "downscale": args.downscale,
        "camera_count": len(eval_cameras),
        "lods": {},
    }

    for lod in args.eval_lods:
        active_count = max(1, min(len(order_np), int(math.ceil(len(order_np) * lod))))
        idx = order_idx[:active_count]
        psnr_values: list[float] = []
        ssim_values: list[float] = []
        lpips_values: list[float] = []
        render_ms_values: list[float] = []

        for cam in eval_cameras:
            c2w, k, width, height = torch_camera(cam, device, args.downscale)
            target = load_target_image(image_path_for_camera(scene_dir, cam.name), width, height, device)

            if device.startswith("cuda"):
                torch.cuda.synchronize()
                start = torch.cuda.Event(enable_timing=True)
                end = torch.cuda.Event(enable_timing=True)
                start.record()
                pred = render_splats(
                    rasterization,
                    splats,
                    c2w,
                    k,
                    width,
                    height,
                    indices=idx,
                    sh_degree=effective_sh_degree(args, splats["sh_degree"]),
                )
                end.record()
                torch.cuda.synchronize()
                render_ms_values.append(float(start.elapsed_time(end)))
            else:
                pred = render_splats(
                    rasterization,
                    splats,
                    c2w,
                    k,
                    width,
                    height,
                    indices=idx,
                    sh_degree=effective_sh_degree(args, splats["sh_degree"]),
                )

            pred_metric = pred.clamp(0.0, 1.0)
            mse = torch.mean((pred_metric - target) ** 2).item()
            psnr_values.append(psnr_from_mse(mse))
            ssim_values.append(global_ssim(pred_metric, target))
            if lpips_metric is not None:
                with torch.no_grad():
                    lp = lpips_metric(
                        pred_metric.permute(2, 0, 1).unsqueeze(0),
                        target.permute(2, 0, 1).unsqueeze(0).clamp(0.0, 1.0),
                    )
                lpips_values.append(float(lp.item()))

        lod_key = f"{lod:.6g}"
        report["lods"][lod_key] = {
            "active_count": active_count,
            "active_ratio": active_count / max(1, len(order_np)),
            "psnr": float(np.mean(psnr_values)) if psnr_values else 0.0,
            "ssim": float(np.mean(ssim_values)) if ssim_values else 0.0,
            "render_ms": float(np.mean(render_ms_values)) if render_ms_values else None,
        }
        if lpips_values:
            report["lods"][lod_key]["lpips"] = float(np.mean(lpips_values))
        print(
            f"[eval lod={lod:.3f}] active={active_count} "
            f"psnr={report['lods'][lod_key]['psnr']:.3f} "
            f"ssim={report['lods'][lod_key]['ssim']:.4f} "
            f"render_ms={report['lods'][lod_key]['render_ms']}"
        )

    if args.eval_output:
        out = Path(args.eval_output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote LOD evaluation report: {out}")
    return report


def gsplat_importance(ply: PlyData, cameras: list[Camera], args: argparse.Namespace) -> np.ndarray:
    configure_conda_cuda_home()
    try:
        import torch
        import torch.utils.cpp_extension as cpp_extension

        patch_torch_cpp_extension_for_windows(cpp_extension)
        from gsplat.rendering import rasterization
    except Exception as exc:  # pragma: no cover - depends on user GPU env.
        raise RuntimeError(
            "gsplat mode requires a CUDA PyTorch environment with gsplat installed. "
            "Use --method heuristic for the dependency-light baseline."
        ) from exc

    if not args.scene:
        raise ValueError("--scene is required for gsplat mode because GT images are used")

    device = args.device
    if device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA is not available; use --device cpu only for small debugging runs")

    initial = heuristic_importance(ply, cameras, args)
    splats = torch_splats_from_ply(ply, device)
    n = int(splats["means"].shape[0])

    candidate_count = n if args.train_max_points <= 0 else min(n, args.train_max_points)
    candidate_idx_np = np.argsort(initial)[::-1][:candidate_count].copy()
    candidate_idx = torch.as_tensor(candidate_idx_np, dtype=torch.long, device=device)

    init_scores = np.clip(initial, 1e-4, 1.0 - 1e-4)
    logits_np = np.log(init_scores / (1.0 - init_scores)).astype(np.float32)
    score_logits = torch.nn.Parameter(torch.as_tensor(logits_np, dtype=torch.float32, device=device))
    optimizer = torch.optim.Adam([score_logits], lr=args.lr)

    scene_dir = Path(args.scene)
    rng = random.Random(args.seed)
    torch.manual_seed(args.seed)

    for step in range(1, args.iterations + 1):
        cam = rng.choice(cameras)
        c2w, k, width, height = torch_camera(cam, device, args.downscale)
        target = load_target_image(image_path_for_camera(scene_dir, cam.name), width, height, device)

        lod = rng.uniform(args.min_lod, args.max_lod)
        scores_detached = score_logits.detach()
        kth = max(1, int(math.ceil(n * lod)))
        threshold = torch.topk(scores_detached, kth, largest=True).values[-1]

        local_logits = score_logits[candidate_idx]
        mask = torch.sigmoid((local_logits - threshold) / max(args.temperature, 1e-4))
        active = mask > args.mask_epsilon
        if not bool(active.any()):
            active = torch.ones_like(mask, dtype=torch.bool)

        idx = candidate_idx[active]
        local_mask = mask[active]
        opacities = torch.sigmoid(splats["opacities"][idx]) * local_mask
        pred = render_splats(
            rasterization,
            splats,
            c2w,
            k,
            width,
            height,
            indices=idx,
            opacities_override=opacities,
            colors_override=splats["colors"][idx],
            sh_degree=effective_sh_degree(args, splats["sh_degree"]),
        )

        image_loss = torch.nn.functional.l1_loss(pred, target)
        budget_loss = (mask.mean() - lod) ** 2
        entropy_loss = (mask * (1.0 - mask)).mean()
        loss = image_loss + args.budget_weight * budget_loss + args.entropy_weight * entropy_loss

        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        optimizer.step()

        if step == 1 or step % args.log_interval == 0:
            print(
                f"[{step:05d}/{args.iterations}] "
                f"loss={loss.item():.5f} image={image_loss.item():.5f} "
                f"lod={lod:.3f} active={int(active.sum().item())}/{candidate_count}"
            )

    trained = torch.sigmoid(score_logits).detach().cpu().numpy().astype(np.float32)
    return normalize01(trained)


def finetune_fields(args: argparse.Namespace) -> set[str]:
    fields = {item.strip().lower() for item in args.finetune_fields.split(",") if item.strip()}
    aliases = {"color": "sh", "colors": "sh", "appearance": "sh", "opacity": "opacity"}
    normalized = {aliases.get(field, field) for field in fields}
    if "none" in normalized:
        return set()
    unknown = normalized - {"opacity", "sh"}
    if unknown:
        raise ValueError(f"Unknown --finetune-fields values: {sorted(unknown)}")
    return normalized


def write_trainable_params_to_vertices(
    ply: PlyData,
    vertex_indices: np.ndarray,
    opacities: Any,
    colors: Any,
) -> np.ndarray:
    out = ply.vertices.copy()
    names = set(out.dtype.names or ())
    idx = np.asarray(vertex_indices, dtype=np.int64)

    if "opacity" in names:
        out["opacity"][idx] = opacities.detach().cpu().numpy().astype(np.float32)

    colors_np = colors.detach().cpu().numpy().astype(np.float32)
    if colors_np.ndim == 3 and all(f"f_dc_{i}" in names for i in range(3)):
        for channel in range(3):
            out[f"f_dc_{channel}"][idx] = colors_np[:, 0, channel]

        rest_count = colors_np.shape[1] - 1
        for coeff in range(rest_count):
            for channel in range(3):
                name = f"f_rest_{channel * rest_count + coeff}"
                if name in names:
                    out[name][idx] = colors_np[:, coeff + 1, channel]

    return out


def clod_prefix_finetune(
    ply: PlyData,
    cameras: list[Camera],
    args: argparse.Namespace,
) -> tuple[np.ndarray, np.ndarray]:
    configure_conda_cuda_home()
    try:
        import torch
        import torch.utils.cpp_extension as cpp_extension

        patch_torch_cpp_extension_for_windows(cpp_extension)
        from gsplat.rendering import rasterization
    except Exception as exc:  # pragma: no cover - depends on user GPU env.
        raise RuntimeError("CLOD prefix finetuning requires CUDA PyTorch with gsplat installed") from exc

    if not args.scene:
        raise ValueError("--scene is required for clod mode because GT images are used")

    fields = finetune_fields(args)
    if not fields:
        raise ValueError("clod mode needs at least one trainable field; use --finetune-fields opacity,sh")

    device = args.device
    if device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA is not available; use --device cpu only for small debugging runs")

    initial = importance_from_ply_or_heuristic(ply, cameras, args)
    order_np = np.argsort(initial)[::-1].copy()
    n = len(order_np)
    train_count = n if args.train_max_points <= 0 else min(n, args.train_max_points)
    train_order_np = order_np[:train_count].copy()

    if args.max_lod * n > train_count:
        effective_max = train_count / max(1, n)
        print(
            f"[clod] train-max-points clips random prefixes at {train_count}/{n} "
            f"({effective_max:.3f}); lower --max-lod or raise --train-max-points for full-range training."
        )

    splats = torch_splats_from_ply(ply, device)
    train_order = torch.as_tensor(train_order_np, dtype=torch.long, device=device)
    teacher_count = n if args.teacher_max_points <= 0 else min(n, args.teacher_max_points)
    teacher_idx = None
    if teacher_count < n:
        teacher_idx = torch.as_tensor(order_np[:teacher_count].copy(), dtype=torch.long, device=device)

    prefix_splats: dict[str, Any] = {
        "means": splats["means"][train_order].detach(),
        "quats": splats["quats"][train_order].detach(),
        "scales": splats["scales"][train_order].detach(),
        "opacities": splats["opacities"][train_order].detach().clone(),
        "colors": splats["colors"][train_order].detach().clone(),
        "sh_degree": splats["sh_degree"],
    }

    original_opacity = prefix_splats["opacities"].detach().clone()
    original_colors = prefix_splats["colors"].detach().clone()
    optim_groups: list[dict[str, Any]] = []

    if "opacity" in fields:
        prefix_splats["opacities"] = torch.nn.Parameter(prefix_splats["opacities"])
        optim_groups.append({"params": [prefix_splats["opacities"]], "lr": args.opacity_lr})
    if "sh" in fields:
        prefix_splats["colors"] = torch.nn.Parameter(prefix_splats["colors"])
        optim_groups.append({"params": [prefix_splats["colors"]], "lr": args.sh_lr})

    optimizer = torch.optim.Adam(optim_groups)
    scene_dir = Path(args.scene)
    rng = random.Random(args.seed)
    torch.manual_seed(args.seed)
    sh_degree = effective_sh_degree(args, splats["sh_degree"])

    for step in range(1, args.iterations + 1):
        cam = rng.choice(cameras)
        c2w, k, width, height = torch_camera(cam, device, args.downscale)
        target = load_target_image(image_path_for_camera(scene_dir, cam.name), width, height, device)

        lod = rng.uniform(args.min_lod, args.max_lod)
        prefix_count = max(1, min(train_count, int(math.ceil(n * lod))))
        prefix_idx = torch.arange(prefix_count, dtype=torch.long, device=device)

        with torch.no_grad():
            teacher = render_splats(
                rasterization,
                splats,
                c2w,
                k,
                width,
                height,
                indices=teacher_idx,
                sh_degree=sh_degree,
            )

        pred = render_splats(
            rasterization,
            prefix_splats,
            c2w,
            k,
            width,
            height,
            indices=prefix_idx,
            sh_degree=sh_degree,
        )

        image_loss = torch.nn.functional.l1_loss(pred, target)
        teacher_loss = torch.nn.functional.l1_loss(pred, teacher)
        reg_loss = torch.zeros((), dtype=torch.float32, device=device)
        if args.param_reg_weight > 0.0:
            if "opacity" in fields:
                reg_loss = reg_loss + torch.nn.functional.l1_loss(prefix_splats["opacities"], original_opacity)
            if "sh" in fields:
                reg_loss = reg_loss + torch.nn.functional.l1_loss(prefix_splats["colors"], original_colors)
        loss = args.image_weight * image_loss + args.teacher_weight * teacher_loss + args.param_reg_weight * reg_loss

        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        optimizer.step()

        if step == 1 or step % args.log_interval == 0:
            print(
                f"[{step:05d}/{args.iterations}] "
                f"loss={loss.item():.5f} image={image_loss.item():.5f} "
                f"teacher={teacher_loss.item():.5f} lod={lod:.3f} prefix={prefix_count}/{n}"
            )

    updated_vertices = write_trainable_params_to_vertices(
        ply,
        train_order_np,
        prefix_splats["opacities"],
        prefix_splats["colors"],
    )
    return rank_importance(order_np, n), updated_vertices


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--method",
        choices=["heuristic", "gsplat", "clod"],
        default="heuristic",
    )
    parser.add_argument("--ply", required=True, help="Input Gaussian PLY")
    parser.add_argument("--output", help="Output PLY with an importance property")
    parser.add_argument("--scene", help="Scene root with images/ and optionally sparse/0")
    parser.add_argument("--model", help="Model root containing cameras.json")
    parser.add_argument("--cameras-json", help="Path to GraphDECO cameras.json")
    parser.add_argument("--scores", help="Optional .npy output for raw importance values")
    parser.add_argument(
        "--init-importance",
        choices=["ply", "heuristic"],
        default="ply",
        help="Use an existing importance property when present, or recompute the heuristic order",
    )

    parser.add_argument("--seed", type=int, default=13)
    parser.add_argument("--sample-cameras", type=int, default=96, help="Max cameras for heuristic scoring; <=0 uses all")
    parser.add_argument("--chunk-size", type=int, default=262144)
    parser.add_argument("--near", type=float, default=1e-4)
    parser.add_argument("--frustum-margin", type=float, default=0.05)
    parser.add_argument("--max-projected-area", type=float, default=4096.0)
    parser.add_argument("--opacity-power", type=float, default=1.0)
    parser.add_argument("--size-power", type=float, default=0.15)
    parser.add_argument("--coverage-weight", type=float, default=0.15)
    parser.add_argument("--iterations", type=int, default=2000)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--downscale", type=int, default=4)
    parser.add_argument("--train-max-points", type=int, default=600000)
    parser.add_argument("--min-lod", type=float, default=0.05)
    parser.add_argument("--max-lod", type=float, default=1.0)
    parser.add_argument("--temperature", type=float, default=0.05)
    parser.add_argument("--mask-epsilon", type=float, default=1e-3)
    parser.add_argument("--lr", type=float, default=0.03)
    parser.add_argument("--budget-weight", type=float, default=2.0)
    parser.add_argument("--entropy-weight", type=float, default=0.001)
    parser.add_argument("--sh-degree", type=int, default=-1, help="SH degree for gsplat renders; -1 uses the PLY maximum")

    parser.add_argument("--finetune-fields", default="opacity,sh", help="clod mode trainable fields: opacity, sh, or opacity,sh")
    parser.add_argument("--opacity-lr", type=float, default=0.005)
    parser.add_argument("--sh-lr", type=float, default=0.002)
    parser.add_argument("--image-weight", type=float, default=1.0)
    parser.add_argument("--teacher-weight", type=float, default=0.5)
    parser.add_argument("--teacher-max-points", type=int, default=0, help="0 means full model teacher")
    parser.add_argument("--param-reg-weight", type=float, default=0.001)

    parser.add_argument("--eval-only", action="store_true", help="Only render/evaluate the current PLY across LOD budgets")
    parser.add_argument("--eval-output", help="Optional JSON report for LOD PSNR/SSIM/render time")
    parser.add_argument("--eval-lods", type=float, nargs="*", default=[1.0, 0.5, 0.25, 0.1, 0.05])
    parser.add_argument("--eval-cameras", type=int, default=16, help="Max cameras for LOD evaluation; <=0 uses all")
    parser.add_argument("--eval-lpips", action="store_true", help="Also compute LPIPS when torchmetrics is installed")
    parser.add_argument("--log-interval", type=int, default=25)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    random.seed(args.seed)
    np.random.seed(args.seed)

    ply = read_ply(Path(args.ply))
    cameras = load_cameras(args)
    if not cameras:
        raise ValueError("No cameras loaded")

    if args.eval_only:
        importance = importance_from_ply_or_heuristic(ply, cameras, args)
        evaluate_lod_curve(ply, cameras, args, importance)
        return

    if not args.output:
        raise ValueError("--output is required unless --eval-only is used")

    updated_vertices: np.ndarray | None = None
    if args.method == "heuristic":
        importance = heuristic_importance(ply, cameras, args)
    elif args.method == "clod":
        importance, updated_vertices = clod_prefix_finetune(ply, cameras, args)
    else:
        importance = gsplat_importance(ply, cameras, args)

    out_vertices = with_importance(ply, importance, updated_vertices)
    write_ply(Path(args.output), ply, out_vertices)
    if args.scores:
        Path(args.scores).parent.mkdir(parents=True, exist_ok=True)
        np.save(args.scores, importance)

    order = np.argsort(importance)[::-1]
    print(f"Wrote {args.output}")
    print(
        f"importance: min={importance.min():.6f} max={importance.max():.6f} "
        f"mean={importance.mean():.6f} top0={int(order[0]) if len(order) else -1}"
    )

    if args.eval_output:
        evaluate_lod_curve(ply_with_vertices(ply, out_vertices), cameras, args, importance)


if __name__ == "__main__":
    main()
