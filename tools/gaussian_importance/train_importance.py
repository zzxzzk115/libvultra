#!/usr/bin/env python3
"""Train or estimate per-Gaussian importance for libvultra Ordered CLOD.

The official CLOD-3DGS training code teaches the model to survive random
prefixes: after normal 3DGS training, each iteration randomly renders only the
first N splats. This tool adapts that idea to libvultra's renderer contract,
where training and rendering are intentionally separate and the renderer only
consumes one scalar importance/order per Gaussian.

Two modes are provided:

* heuristic: no CUDA dependency. Produces a strong first ordering from opacity,
  projected footprint, color energy and multi-view coverage.
* gsplat: optional differentiable score training. Gaussian parameters stay
  frozen; only one score per splat is optimized under random LOD budgets.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import struct
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


def with_importance(ply: PlyData, importance: np.ndarray) -> np.ndarray:
    importance = np.asarray(importance, dtype=np.float32)
    if importance.shape != (ply.vertex_count,):
        raise ValueError(f"importance has shape {importance.shape}, expected {(ply.vertex_count,)}")

    old = ply.vertices
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
    positions = np.stack([v["x"], v["y"], v["z"]], axis=-1).astype(np.float32)
    quats = np.stack([get_field(v, f"rot_{i}", 0.0) for i in range(4)], axis=-1).astype(np.float32)
    scales = np.stack([get_field(v, f"scale_{i}", -7.0) for i in range(3)], axis=-1).astype(np.float32)
    opacities = get_field(v, "opacity", 10.0).astype(np.float32)
    sh0 = np.stack([get_field(v, f"f_dc_{i}", 0.0) for i in range(3)], axis=-1).astype(np.float32)

    # gsplat accepts RGB colors directly. For this first importance-training pass
    # we freeze appearance to the DC spherical-harmonic color and only optimize the
    # per-Gaussian score logits that later drive Vultra's Ordered LOD renderer.
    colors = np.clip(0.5 + SH_C0 * sh0, 0.0, 1.0).astype(np.float32)

    return {
        "means": torch.as_tensor(positions, device=device),
        "quats": torch.as_tensor(quats, device=device),
        "scales": torch.as_tensor(scales, device=device),
        "opacities": torch.as_tensor(opacities, device=device),
        "colors": torch.as_tensor(colors, device=device),
    }


def configure_conda_cuda_home() -> None:
    os.environ.setdefault("VSLANG", "1033")
    if os.name == "nt":
        os.environ.setdefault("NVCC_PREPEND_FLAGS", "--use-local-env")
    path_parts = [str(Path(sys.prefix) / "Scripts")]

    vs_roots = [
        Path(root) / "Microsoft Visual Studio" / "2022"
        for root in [os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)")]
        if root
    ]
    for root in vs_roots:
        if not root.exists():
            continue
        cl_candidates = sorted(root.glob(r"*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe"), reverse=True)
        if cl_candidates:
            path_parts.append(str(cl_candidates[0].parent))
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
        means = splats["means"][idx]
        quats = splats["quats"][idx]
        scales = torch.exp(torch.clamp(splats["scales"][idx], -20.0, 20.0))
        opacities = torch.sigmoid(splats["opacities"][idx]) * local_mask
        colors = splats["colors"][idx]

        render_colors, _, _ = rasterization(
            means=means,
            quats=quats,
            scales=scales,
            opacities=opacities,
            colors=colors,
            viewmats=torch.linalg.inv(c2w),
            Ks=k,
            width=width,
            height=height,
            packed=True,
            rasterize_mode="classic",
        )

        pred = render_colors[0, ..., :3]
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--method", choices=["heuristic", "gsplat"], default="heuristic")
    parser.add_argument("--ply", required=True, help="Input Gaussian PLY")
    parser.add_argument("--output", required=True, help="Output PLY with an importance property")
    parser.add_argument("--scene", help="Scene root with images/ and optionally sparse/0")
    parser.add_argument("--model", help="Model root containing cameras.json")
    parser.add_argument("--cameras-json", help="Path to GraphDECO cameras.json")
    parser.add_argument("--scores", help="Optional .npy output for raw importance values")

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

    if args.method == "heuristic":
        importance = heuristic_importance(ply, cameras, args)
    else:
        importance = gsplat_importance(ply, cameras, args)

    out_vertices = with_importance(ply, importance)
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


if __name__ == "__main__":
    main()
