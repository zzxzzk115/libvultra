from __future__ import annotations

import json
import itertools
from dataclasses import dataclass
from typing import Any, Iterator
from urllib import request
from urllib.error import URLError


class VultraRpcError(RuntimeError):
    pass


@dataclass
class VultraClient:
    url: str
    timeout: float = 10.0

    _ids = itertools.count(1)

    def call(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        payload = {
            "jsonrpc": "2.0",
            "id": next(self._ids),
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments or {}},
        }
        data = json.dumps(payload).encode("utf-8")
        req = request.Request(
            self.url,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with request.urlopen(req, timeout=self.timeout) as resp:
                response = json.loads(resp.read().decode("utf-8"))
        except URLError as exc:
            raise VultraRpcError(f"failed to call Vultra RPC: {exc}") from exc

        if "error" in response:
            error = response["error"]
            raise VultraRpcError(error.get("message", str(error)))

        result = response.get("result", {})
        if result.get("isError"):
            text = _first_text(result)
            raise VultraRpcError(_payload_text_error(text))

        text = _first_text(result)
        if not text:
            return {}
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"ok": True, "text": text}


class Simulation:
    def __init__(self, client: VultraClient):
        self.client = client

    @classmethod
    def connect(cls, url: str = "http://127.0.0.1:8848/mcp", timeout: float = 10.0) -> "Simulation":
        return cls(VultraClient(url=url, timeout=timeout))

    def status(self) -> dict[str, Any]:
        return self.client.call("vultra.runtime.status")

    def reset(self, scene: str | None = None, seed: int | None = None, play: bool = False) -> dict[str, Any]:
        args: dict[str, Any] = {"play": play}
        if scene is not None:
            args["scene"] = scene
        if seed is not None:
            args["seed"] = seed
        return self.client.call("vultra.sim.reset", args)

    def step(
        self,
        actions: list[dict[str, Any]] | None = None,
        frames: int = 1,
        include_state: bool = True,
        limit: int = 1024,
    ) -> dict[str, Any]:
        args: dict[str, Any] = {
            "frames": frames,
            "includeState": include_state,
            "limit": limit,
        }
        if actions is not None:
            args["actions"] = actions
        return self.client.call("vultra.sim.step", args)

    def get_state_batch(
        self,
        entities: list[int | str] | None = None,
        limit: int = 1024,
        include_inactive: bool = True,
        include_velocity: bool = True,
    ) -> dict[str, Any]:
        args: dict[str, Any] = {
            "limit": limit,
            "includeInactive": include_inactive,
            "includeVelocity": include_velocity,
        }
        if entities is not None:
            args["entities"] = entities
        return self.client.call("vultra.sim.get_state_batch", args)

    def set_state_batch(self, states: list[dict[str, Any]]) -> dict[str, Any]:
        return self.client.call("vultra.sim.set_state_batch", {"states": states})

    def apply_actions_batch(self, actions: list[dict[str, Any]]) -> dict[str, Any]:
        return self.client.call("vultra.sim.apply_actions_batch", {"actions": actions})

    def capture_rgb(
        self,
        output_file: str | None = None,
        camera: str | None = None,
        width: int | None = None,
        height: int | None = None,
    ) -> dict[str, Any]:
        args: dict[str, Any] = {}
        if output_file is not None:
            args["outputFile"] = output_file
        if camera is not None:
            args["camera"] = camera
        if width is not None:
            args["width"] = width
        if height is not None:
            args["height"] = height
        return self.client.call("vultra.render.capture_rgb", args)

    def capture_depth(self, output_directory: str | None = None, camera: str | None = None) -> dict[str, Any]:
        args: dict[str, Any] = {}
        if output_directory is not None:
            args["outputDirectory"] = output_directory
        if camera is not None:
            args["camera"] = camera
        return self.client.call("vultra.render.capture_depth", args)

    def start_stream(
        self,
        fps: int = 0,
        jpeg_quality: int = 80,
        max_width: int | None = None,
        max_height: int | None = None,
    ) -> dict[str, Any]:
        args: dict[str, Any] = {"action": "start", "fps": fps, "jpegQuality": jpeg_quality}
        if max_width is not None:
            args["maxWidth"] = max_width
        if max_height is not None:
            args["maxHeight"] = max_height
        return self.client.call(
            "vultra.render.stream",
            args,
        )

    def stream_status(self) -> dict[str, Any]:
        return self.client.call("vultra.render.stream", {"action": "status"})

    def stop_stream(self) -> dict[str, Any]:
        return self.client.call("vultra.render.stream", {"action": "stop"})

    def iter_mjpeg_frames(
        self,
        url: str | None = None,
        max_frames: int | None = None,
        timeout: float | None = None,
    ) -> Iterator[bytes]:
        if url is None:
            status = self.stream_status()
            url = status.get("stream", {}).get("url", "")
        if not url:
            raise VultraRpcError("video stream URL is unavailable")

        frame_count = 0
        buffer = b""
        try:
            with request.urlopen(url, timeout=self.client.timeout if timeout is None else timeout) as resp:
                while max_frames is None or frame_count < max_frames:
                    chunk = resp.read(65536)
                    if not chunk:
                        break
                    buffer += chunk
                    while True:
                        start = buffer.find(b"\xff\xd8")
                        if start < 0:
                            buffer = buffer[-2:]
                            break
                        end = buffer.find(b"\xff\xd9", start + 2)
                        if end < 0:
                            buffer = buffer[start:]
                            break
                        frame = buffer[start:end + 2]
                        buffer = buffer[end + 2:]
                        frame_count += 1
                        yield frame
                        if max_frames is not None and frame_count >= max_frames:
                            return
        except URLError as exc:
            raise VultraRpcError(f"failed to read Vultra video stream: {exc}") from exc


def _first_text(result: dict[str, Any]) -> str:
    content = result.get("content", [])
    if not content:
        return ""
    first = content[0]
    if not isinstance(first, dict):
        return ""
    return str(first.get("text", ""))


def _payload_text_error(text: str) -> str:
    try:
        payload = json.loads(text)
        return payload.get("error", text)
    except json.JSONDecodeError:
        return text
