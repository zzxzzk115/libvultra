"""Safe owners for the optional vultra-research native library.

Use context managers, serialize all hosts on one thread, and use separate processes
for concurrent scans. NumPy images own their memory and survive session closure.
"""

import ctypes
from dataclasses import dataclass
import json
import math
import operator
from pathlib import Path
import threading
import weakref

import numpy as np

from ._bindings_generated import (
    ABI_VERSION, VultraExperimentApi, VultraExperimentImageInfo, VultraExperimentProgress,
)


class ResearchError(RuntimeError):
    """A native experiment rejected an operation or failed to execute it."""


@dataclass(frozen=True)
class Progress:
    frames: int
    seconds: float
    time_step: float


class _Host(ctypes.Structure):
    # Only bootstrap ownership is handwritten; the function table and POD layouts are generated.
    _fields_ = [
        ("owner", ctypes.c_void_p),
        ("context", ctypes.c_void_p),
        ("experiment", ctypes.POINTER(VultraExperimentApi)),
    ]


def _text(value):
    if not isinstance(value, str):
        raise TypeError("Expected UTF-8 text")
    if "\0" in value:
        raise ValueError("Text cannot contain a NUL byte")
    return value.encode("utf-8")


def _positive(value, bits, name):
    value = operator.index(value)
    if value <= 0 or value >= 1 << bits:
        raise ValueError(f"{name} must be positive and fit uint{bits}")
    return value


class Research:
    """Own the native device and multiple independent experiment sessions."""

    def __init__(self, library, *, ray_tracing=False):
        self._thread = threading.get_ident()
        self._closed = True
        self._sessions = weakref.WeakSet()
        self._native = _Host()
        self._library = ctypes.CDLL(str(Path(library).resolve()))
        self._create = self._library.vultra_research_create
        self._create.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(_Host)]
        self._create.restype = ctypes.c_int32
        self._destroy = self._library.vultra_research_destroy
        self._destroy.argtypes = [ctypes.POINTER(_Host)]
        self._destroy.restype = None
        features = (1 << 6) | (1 << 3) if ray_tracing else 0  # VRI RayQuery | Bindless.
        status = self._create(ABI_VERSION, ctypes.sizeof(VultraExperimentApi), features, ctypes.byref(self._native))
        if status != 0:
            raise ResearchError(f"Create research host failed (status {status}); inspect native diagnostics")
        self._api = self._native.experiment.contents
        if self._api.version != ABI_VERSION or self._api.struct_size != ctypes.sizeof(VultraExperimentApi):
            self._destroy(ctypes.byref(self._native))
            raise ResearchError("Incompatible version-1 experiment function table; rebuild the native library")
        self._closed = False

    def _check(self):
        if self._closed:
            raise ResearchError("Research host is closed")
        if threading.get_ident() != self._thread:
            raise ResearchError("Research calls must stay on the creating thread")

    def _status(self, status):
        if status == 0:
            return
        data = ctypes.c_void_p()
        size = ctypes.c_uint64()
        result = self._api.last_error(self._native.context, ctypes.byref(data), ctypes.byref(size))
        message = ctypes.string_at(data.value, size.value).decode("utf-8") if result == 0 and size.value else ""
        raise ResearchError(message or f"Experiment operation failed (status {status})")

    def _string(self, function, *arguments):
        data = ctypes.c_void_p()
        size = ctypes.c_uint64()
        self._status(function(self._native.context, *arguments, ctypes.byref(data), ctypes.byref(size)))
        return ctypes.string_at(data.value, size.value).decode("utf-8") if size.value else ""

    def open(self, input, *, width=1280, height=720, path="deferred", time_step=1 / 60, environment=None, seed=0):
        self._check()
        width = _positive(width, 32, "width")
        height = _positive(height, 32, "height")
        time_step = float(time_step)
        if not math.isfinite(time_step) or time_step <= 0:
            raise ValueError("time_step must be finite and positive")
        paths = {"deferred": 0, "forward": 1, "reference": 2}
        if path not in paths:
            raise ValueError("path must be deferred, forward or reference")
        seed = operator.index(seed)
        if seed < 0 or seed >= 1 << 32:
            raise ValueError("seed must fit uint32")
        source = _text(str(Path(input).resolve()))
        hdr = _text(str(Path(environment).resolve())) if environment is not None else b""
        identifier = ctypes.c_uint64()
        self._status(self._api.open(
            self._native.context, source, len(source), width, height, paths[path], time_step,
            hdr, len(hdr), seed, ctypes.byref(identifier),
        ))
        session = Session(self, identifier.value)
        self._sessions.add(session)
        return session

    def run(self, description):
        """Run a native .vexperiment (including warmup), returning its completed session."""
        self._check()
        file = _text(str(Path(description).resolve()))
        identifier = ctypes.c_uint64()
        self._status(self._api.run_description(
            self._native.context, file, len(file), ctypes.byref(identifier),
        ))
        session = Session(self, identifier.value)
        self._sessions.add(session)
        return session

    def close(self):
        if self._closed:
            return
        self._check()
        for session in list(self._sessions):
            session.close()
        self._destroy(ctypes.byref(self._native))
        self._closed = True

    def __enter__(self):
        self._check()
        return self

    def __exit__(self, *_):
        self.close()


class Session:
    """A project/model/VPK, its script host, scene, catalog and completed GPU outputs."""

    def __init__(self, host, identifier):
        self._host = host
        self._id = identifier
        self._closed = False

    def _check(self):
        self._host._check()
        if self._closed:
            raise ResearchError("Experiment session is closed")

    def step(self, frames=1):
        self._check()
        frames = _positive(frames, 64, "frames")
        self._host._status(self._host._api.step(self._host._native.context, self._id, frames))

    def set_graph(self, definition):
        self._check()
        text = _text(json.dumps(definition, ensure_ascii=False, allow_nan=False))
        self._host._status(self._host._api.set_graph(self._host._native.context, self._id, text, len(text)))

    def load_graph(self, file):
        self.set_graph(json.loads(Path(file).read_text(encoding="utf-8")))

    def set_parameter(self, pass_name, parameter, value):
        self._check()
        name = _text(pass_name)
        field = _text(parameter)
        value = float(value)
        if not math.isfinite(value):
            raise ValueError("Pass parameters must be finite")
        self._host._status(self._host._api.set_parameter(
            self._host._native.context, self._id, name, len(name), field, len(field), value,
        ))

    def image(self, output="final"):
        """Return a new top-left-origin H×W×4 float32 array, with no implicit color conversion."""
        self._check()
        name = _text(output)
        info = VultraExperimentImageInfo()
        self._host._status(self._host._api.image_info(
            self._host._native.context, self._id, name, len(name), ctypes.byref(info),
        ))
        if not info.width or not info.height or info.floatCount != info.width * info.height * 4:
            raise ResearchError("Invalid native image descriptor")
        image = np.empty((info.height, info.width, 4), dtype=np.float32)
        self._host._status(self._host._api.read_image(
            self._host._native.context, self._id, name, len(name),
            image.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), image.size,
        ))
        return image

    @property
    def progress(self):
        self._check()
        value = VultraExperimentProgress()
        self._host._status(self._host._api.progress(self._host._native.context, self._id, ctypes.byref(value)))
        return Progress(value.frames, value.seconds, value.timeStep)

    @property
    def report(self):
        """Completed frame graph, device, simulation state and inclusive CPU/GPU events."""
        self._check()
        return json.loads(self._host._string(self._host._api.report_snapshot, self._id))

    def save_report(self, directory, *, revision, outputs=("hdr",)):
        """Export linear RGB PFM arrays and a version-1 report into a new directory."""
        if not isinstance(revision, str) or not revision:
            raise ValueError("revision must identify the source/build")
        report = self.report
        images = [(name, self.image(name)) for name in outputs]
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=False)
        report["source_revision"] = revision
        capture_metadata = {image["port"]: image for image in report["images"]}
        report["images"] = []
        for index, (name, image) in enumerate(images):
            file = f"output_{index:03}.pfm"
            with (directory / file).open("wb") as stream:
                stream.write(f"PF\n{image.shape[1]} {image.shape[0]}\n-1.0\n".encode("ascii"))
                stream.write(image[::-1, :, :3].astype("<f4").tobytes())
            capture = dict(capture_metadata[name])
            capture["file"] = file
            report["images"].append(capture)
        (directory / "graph_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return report

    @property
    def scene(self):
        self._check()
        return json.loads(self._host._string(self._host._api.scene_snapshot, self._id))

    def close(self):
        if self._closed:
            return
        self._check()
        self._host._status(self._host._api.close(self._host._native.context, self._id))
        self._closed = True
        self._host._sessions.discard(self)

    def __enter__(self):
        self._check()
        return self

    def __exit__(self, *_):
        self.close()
