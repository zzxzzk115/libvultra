"""Shared IR (de)serialization + lightweight validation.

Every stage and backend goes through here so the on-disk contract lives in one
place. Validation is intentionally dependency-free (no jsonschema) to preserve
the build-time-Python-free guarantee; bindings.ir.schema.json documents the
shape for humans and future stricter validation.
"""

from __future__ import annotations

import json
from pathlib import Path

from . import SCHEMA_VERSION
from . import ir_model as m


def dump(ir: m.IR, path: Path) -> bool:
    """Write the IR as pretty JSON. write-if-changed: returns True iff written
    (so an unchanged regen does not bump mtime and trigger needless backend work)."""
    text = json.dumps(ir.to_json_dict(), indent=2, ensure_ascii=False) + "\n"
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    return True


def load(path: Path) -> m.IR:
    if not path.exists():
        raise FileNotFoundError(f"IR not found: {path} (run extract_bindings.py)")
    raw = json.loads(path.read_text(encoding="utf-8"))
    version = raw.get("schemaVersion")
    if version != SCHEMA_VERSION:
        raise ValueError(
            f"IR schemaVersion {version} != backend {SCHEMA_VERSION}; regenerate the IR")
    return m.from_json_dict(raw)
