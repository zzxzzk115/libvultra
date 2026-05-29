#!/usr/bin/env python3
"""Minimal stdio MCP server for Vultra dual-layer AI Harness."""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import sys
from pathlib import Path
from typing import Any


PROTOCOL_VERSION = "2025-03-26"


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8") if path.exists() else ""


def safe_slug(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"[^a-z0-9._-]+", "-", text)
    text = text.strip("-._")
    return text or "task"


def json_text(value: Any) -> str:
    return json.dumps(value, indent=2, ensure_ascii=False)


def text_content(text: str, uri: str = "vultra://response", mime: str = "text/plain") -> dict[str, Any]:
    return {"contents": [{"uri": uri, "mimeType": mime, "text": text}]}


def tool_text(text: str) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": text}]}


def resource(uri: str, name: str, description: str, mime: str = "text/plain") -> dict[str, str]:
    return {"uri": uri, "name": name, "description": description, "mimeType": mime}


def list_files(root: Path, patterns: tuple[str, ...]) -> list[str]:
    if not root.exists():
        return []
    out: list[str] = []
    for pattern in patterns:
        for path in root.rglob(pattern):
            if path.is_file():
                out.append(path.relative_to(root).as_posix())
    return sorted(set(out))


def find_vproject(path: Path) -> Path | None:
    if path.is_file() and path.suffix == ".vproject":
        return path
    if not path.exists():
        return None
    if path.is_dir():
        files = sorted(path.glob("*.vproject"))
        return files[0] if files else None
    return None


def parse_vproject(path: Path | None) -> dict[str, Any]:
    if path is None or not path.exists():
        return {}
    result: dict[str, Any] = {"path": path.as_posix(), "project_dir": path.parent.as_posix()}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("[") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip().strip('"')
        result[key.strip()] = value
    result.setdefault("asset_root", "resources")
    result.setdefault("default_scene", "")
    result.setdefault("editing_rendergraph", "res://render/default.vrg.json")
    return result


class VultraMcp:
    def __init__(self, engine_root: Path, project_root: Path | None):
        self.engine_root = engine_root.resolve()
        if project_root is None:
            cwd_project = find_vproject(Path.cwd())
            project_root = cwd_project.parent if cwd_project else self.engine_root
        self.project_root = project_root.resolve()
        self.vproject_path = find_vproject(self.project_root)

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        if "id" not in message:
            return None
        method = message.get("method")
        params = message.get("params") or {}
        try:
            if method == "initialize":
                result = self.initialize()
            elif method == "resources/list":
                result = self.resources_list()
            elif method == "resources/read":
                result = self.resources_read(params.get("uri", ""))
            elif method == "prompts/list":
                result = self.prompts_list()
            elif method == "prompts/get":
                result = self.prompts_get(params.get("name", ""), params.get("arguments") or {})
            elif method == "tools/list":
                result = self.tools_list()
            elif method == "tools/call":
                result = self.tools_call(params.get("name", ""), params.get("arguments") or {})
            else:
                return self.error(message["id"], -32601, f"Method not found: {method}")
            return {"jsonrpc": "2.0", "id": message["id"], "result": result}
        except Exception as exc:  # Keep MCP errors structured.
            return self.error(message["id"], -32000, str(exc))

    def error(self, request_id: Any, code: int, message: str) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}

    def initialize(self) -> dict[str, Any]:
        return {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {
                "resources": {},
                "prompts": {},
                "tools": {},
            },
            "serverInfo": {"name": "vultra-mcp", "version": "0.1.0"},
        }

    def resources_list(self) -> dict[str, Any]:
        return {
            "resources": [
                resource("vultra://engine/specs", "Engine Specs", "Tracked engine Harness specs."),
                resource("vultra://engine/tasks", "Engine Tasks", "Tracked engine task files."),
                resource("vultra://engine/skills", "Engine Skills", "Repository skills."),
                resource("vultra://project/current", "Current Project", "Resolved project root and .vproject data.", "application/json"),
                resource("vultra://project/vproject", "VProject", "Raw .vproject file."),
                resource("vultra://project/game", "Game Brief", "Project ai/game.md."),
                resource("vultra://project/assets", "Project Assets", "Project asset file index.", "application/json"),
                resource("vultra://project/scenes", "Project Scenes", "Project scene file index.", "application/json"),
                resource("vultra://project/rendergraphs", "Project Render Graphs", "Project render graph file index.", "application/json"),
                resource("vultra://project/scripts", "Project Scripts", "Project script file index.", "application/json"),
            ]
        }

    def resources_read(self, uri: str) -> dict[str, Any]:
        def out(text: str, mime: str = "text/plain") -> dict[str, Any]:
            return text_content(text, uri, mime)

        if uri == "vultra://engine/specs":
            return out(json_text(list_files(self.engine_root / "ai" / "specs", ("*.md",))), "application/json")
        if uri == "vultra://engine/tasks":
            return out(json_text(list_files(self.engine_root / "ai" / "tasks", ("*.md",))), "application/json")
        if uri == "vultra://engine/skills":
            files = list_files(self.engine_root / "ai" / "skills", ("SKILL.md",))
            index = read_text(self.engine_root / "ai" / "skills" / "README.md")
            return out(index + "\n\nRepository skill files:\n" + json_text(files))
        if uri.startswith("vultra://engine/knowledge/"):
            name = uri.rsplit("/", 1)[-1]
            return out(read_text(self.engine_root / "ai" / "knowledge" / f"{safe_slug(name)}.md"))
        if uri == "vultra://project/current":
            data = parse_vproject(self.vproject_path)
            data["root"] = self.project_root.as_posix()
            return out(json_text(data), "application/json")
        if uri == "vultra://project/vproject":
            return out(read_text(self.vproject_path) if self.vproject_path else "")
        if uri == "vultra://project/game":
            return out(read_text(self.project_root / "ai" / "game.md"))
        if uri == "vultra://project/assets":
            asset_root = self.project_root / parse_vproject(self.vproject_path).get("asset_root", "resources")
            return out(json_text(list_files(asset_root, ("*",))), "application/json")
        if uri == "vultra://project/scenes":
            return out(json_text(list_files(self.project_root, ("*.vscn",))), "application/json")
        if uri.startswith("vultra://project/scene/"):
            rel = uri.removeprefix("vultra://project/scene/")
            path = (self.project_root / rel).resolve()
            self.ensure_read_inside(self.project_root, path)
            return out(read_text(path))
        if uri == "vultra://project/rendergraphs":
            return out(json_text(list_files(self.project_root / "resources" / "render", ("*.json", "*.lua"))), "application/json")
        if uri.startswith("vultra://project/rendergraph/"):
            rel = uri.removeprefix("vultra://project/rendergraph/")
            path = (self.project_root / rel).resolve()
            self.ensure_read_inside(self.project_root, path)
            return out(read_text(path))
        if uri == "vultra://project/scripts":
            return out(json_text(list_files(self.project_root / "scripts", ("*.lua", "*.py", "*.js"))), "application/json")
        if uri.startswith("vultra://project/knowledge/"):
            name = uri.rsplit("/", 1)[-1]
            return out(read_text(self.project_root / "ai" / "knowledge" / f"{safe_slug(name)}.md"))
        raise ValueError(f"Unknown resource URI: {uri}")

    def prompts_list(self) -> dict[str, Any]:
        names = [
            "vultra.plan_engine_feature",
            "vultra.plan_component_change",
            "vultra.plan_lua_render_pass",
            "vultra.plan_binding",
            "vultra.plan_game_task",
            "vultra.verify_change",
        ]
        return {"prompts": [{"name": name, "description": name.replace(".", " ")} for name in names]}

    def prompts_get(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        topic = arguments.get("topic", "the requested work")
        body = (
            f"Plan {topic} for Vultra using the dual-layer AI Harness. "
            "Read relevant specs, choose a skill, define scope, propose verification, "
            "and keep engine and project knowledge separate."
        )
        if name.endswith("verify_change"):
            body = f"Verify {topic}. Prefer focused xmake, asset import/pack, and editor/runtime checks."
        return {"messages": [{"role": "user", "content": {"type": "text", "text": body}}]}

    def tools_list(self) -> dict[str, Any]:
        schema = {
            "type": "object",
            "properties": {
                "layer": {"type": "string", "enum": ["engine", "project"]},
                "relative_path": {"type": "string"},
                "content": {"type": "string"},
                "mode": {"type": "string", "enum": ["dry_run", "write"], "default": "dry_run"},
                "expected_sha256": {"type": "string"},
            },
            "required": ["relative_path", "content"],
        }
        return {
            "tools": [
                {"name": "vultra.inspect_project", "description": "Inspect current Vultra project.", "inputSchema": {"type": "object"}},
                {"name": "vultra.bootstrap_context", "description": "Return the required Vultra Harness entry context for an agent session.", "inputSchema": {"type": "object"}},
                {"name": "vultra.index_assets", "description": "Index project assets.", "inputSchema": {"type": "object"}},
                {"name": "vultra.inspect_scene", "description": "Read a project scene file by project-relative path.", "inputSchema": {"type": "object", "properties": {"relative_path": {"type": "string"}}, "required": ["relative_path"]}},
                {"name": "vultra.inspect_rendergraph", "description": "Read a project render graph file by project-relative path.", "inputSchema": {"type": "object", "properties": {"relative_path": {"type": "string"}}, "required": ["relative_path"]}},
                {"name": "vultra.recommend_skill", "description": "Recommend a Vultra skill for a task.", "inputSchema": {"type": "object", "properties": {"request": {"type": "string"}}}},
                {"name": "vultra.propose_harness_patch", "description": "Dry-run or write an engine/project Harness file.", "inputSchema": schema},
                {"name": "vultra.propose_scene_patch", "description": "Dry-run or write a project .vscn file.", "inputSchema": schema},
                {"name": "vultra.propose_rendergraph_patch", "description": "Dry-run or write a project render graph file.", "inputSchema": schema},
                {"name": "vultra.create_task", "description": "Create an engine or project task file.", "inputSchema": {"type": "object", "properties": {"layer": {"type": "string", "enum": ["engine", "project"]}, "title": {"type": "string"}, "body": {"type": "string"}, "mode": {"type": "string"}, "expected_sha256": {"type": "string"}}, "required": ["layer", "title", "body"]}},
                {"name": "vultra.record_journal", "description": "Record engine or project workspace journal.", "inputSchema": {"type": "object", "properties": {"layer": {"type": "string", "enum": ["engine", "project"]}, "title": {"type": "string"}, "body": {"type": "string"}, "mode": {"type": "string"}, "expected_sha256": {"type": "string"}}, "required": ["layer", "title", "body"]}},
            ]
        }

    def tools_call(self, name: str, args: dict[str, Any]) -> dict[str, Any]:
        if name == "vultra.inspect_project":
            return tool_text(self.resources_read("vultra://project/current")["contents"][0]["text"])
        if name == "vultra.bootstrap_context":
            return tool_text(json_text(self.bootstrap_context()))
        if name == "vultra.index_assets":
            return tool_text(self.resources_read("vultra://project/assets")["contents"][0]["text"])
        if name == "vultra.inspect_scene":
            path = args["relative_path"]
            return tool_text(self.resources_read(f"vultra://project/scene/{path}")["contents"][0]["text"])
        if name == "vultra.inspect_rendergraph":
            path = args["relative_path"]
            return tool_text(self.resources_read(f"vultra://project/rendergraph/{path}")["contents"][0]["text"])
        if name == "vultra.recommend_skill":
            return tool_text(self.recommend_skill(args.get("request", "")))
        if name == "vultra.create_task":
            layer = args.get("layer", "engine")
            path = f"ai/tasks/{safe_slug(args.get('title', 'task'))}.md"
            return tool_text(json_text(self.apply_controlled(layer, path, args.get("body", ""), args)))
        if name == "vultra.record_journal":
            layer = args.get("layer", "engine")
            path = f"ai/workspace/{safe_slug(args.get('title', 'journal'))}.md"
            return tool_text(json_text(self.apply_controlled(layer, path, args.get("body", ""), args)))
        if name in {"vultra.propose_harness_patch", "vultra.propose_scene_patch", "vultra.propose_rendergraph_patch"}:
            return tool_text(json_text(self.apply_controlled(args.get("layer", "project"), args["relative_path"], args["content"], args)))
        raise ValueError(f"Unknown tool: {name}")

    def recommend_skill(self, request: str) -> str:
        lower = request.lower()
        if any(word in lower for word in ["binding", "lua api", "script binding"]):
            return "binding-gen"
        if any(word in lower for word in ["component", "vscn", "inspector", "scene serialization"]):
            return "vultra-component-workflow"
        if any(word in lower for word in ["render graph", "render pass", "shader", "vrg", "vrp"]):
            return "vultra-lua-render-pass"
        return "No exact existing skill matched. Start with ai/specs/ai-harness.md and create a focused task."

    def bootstrap_context(self) -> dict[str, Any]:
        project_data = parse_vproject(self.vproject_path)
        is_engine_project = self.project_root == self.engine_root
        project_ai = self.project_root / "ai"
        return {
            "mode": "engine" if is_engine_project else "project",
            "engine_root": self.engine_root.as_posix(),
            "project_root": self.project_root.as_posix(),
            "vproject": project_data,
            "must_read": [
                "AGENTS.md",
                "ai/README.md",
                "ai/specs/ai-harness.md",
                "ai/skills/README.md",
                "relevant ai/knowledge/*.md",
                "relevant ai/tasks/*.md",
            ],
            "project_must_read": [
                "ai/README.md",
                "ai/game.md",
                "relevant ai/specs/*.md",
                "relevant ai/tasks/*.md",
            ] if project_ai.exists() else [],
            "rules": [
                "Keep engine Harness and project Harness separate.",
                "Project agents must not directly edit Vultra engine source.",
                "Prefer dry-run proposals before project content writes.",
                "Record verification and handoff notes in ai/workspace/.",
                "Promote repeated stable facts to ai/knowledge/.",
                "Promote repeated workflows to skills.",
                "Preserve unrelated user changes in the worktree.",
            ],
            "common_verification": [
                "xmake build -y vultra-app",
                "xmake build -y <target>",
                "asset import or pack scripts when resources change",
                "MCP smoke tests when tools/vultra_mcp changes",
            ],
        }

    def apply_controlled(self, layer: str, relative_path: str, content: str, args: dict[str, Any]) -> dict[str, Any]:
        mode = args.get("mode", "dry_run")
        root = self.engine_root if layer == "engine" else self.project_root
        target = (root / relative_path).resolve()
        self.ensure_allowed(layer, root, target)
        old = read_text(target)
        actual_hash = sha256_text(old) if target.exists() else ""
        diff = "\n".join(
            difflib.unified_diff(
                old.splitlines(),
                content.splitlines(),
                fromfile=relative_path + " (current)",
                tofile=relative_path + " (proposed)",
                lineterm="",
            )
        )
        result = {
            "mode": mode,
            "layer": layer,
            "path": target.as_posix(),
            "current_sha256": actual_hash,
            "proposed_sha256": sha256_text(content),
            "diff": diff,
        }
        if mode != "write":
            return result
        expected = args.get("expected_sha256")
        if expected is None:
            raise ValueError("write mode requires expected_sha256")
        if expected != actual_hash:
            raise ValueError("expected_sha256 does not match current file")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8", newline="\n")
        result["written"] = True
        return result

    def ensure_allowed(self, layer: str, root: Path, target: Path) -> None:
        try:
            rel = target.relative_to(root).as_posix()
        except ValueError as exc:
            raise ValueError("target path escapes workspace root") from exc

        if layer == "engine":
            if rel == "ai" or rel.startswith("ai/"):
                return
            raise ValueError("engine writes are limited to ai/**")

        allowed = (
            rel == "ai" or rel.startswith("ai/") or
            (rel.startswith("resources/") and rel.endswith(".vscn")) or
            rel.startswith("resources/render/") or
            rel.startswith("resources/shaders/") or
            rel.startswith("scripts/")
        )
        if not allowed:
            raise ValueError("project writes are limited to ai/**, scenes, render, shaders, and scripts")

    def ensure_read_inside(self, root: Path, target: Path) -> None:
        try:
            target.relative_to(root)
        except ValueError as exc:
            raise ValueError("target path escapes workspace root") from exc


def read_message() -> dict[str, Any] | None:
    headers: dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, _, value = line.decode("ascii", errors="replace").partition(":")
        headers[key.lower()] = value.strip()
    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def write_message(message: dict[str, Any]) -> None:
    body = json.dumps(message, ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine-root", default=".")
    parser.add_argument("--project")
    ns = parser.parse_args()
    server = VultraMcp(Path(ns.engine_root), Path(ns.project) if ns.project else None)
    while True:
        message = read_message()
        if message is None:
            return 0
        response = server.handle(message)
        if response is not None:
            write_message(response)


if __name__ == "__main__":
    raise SystemExit(main())
