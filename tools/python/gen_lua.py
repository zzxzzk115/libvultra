#!/usr/bin/env python3
"""Stage 2 (Lua): IR -> sol2 registration .gen.cpp + LuaLS stub sections.

Loads tools/bindings/ir/bindings.ir.json and runs the Lua backends:
  * lua_sol2     -> source/.../bindings/script_<area>_binding.gen.cpp (per module)
  * lua_typestub -> a managed section of tools/lua-stubs/vultra.lua

Runs independently of Stage 1 so adding a future backend never re-parses C++.
The outputs are checked in (no build-time Python dependency).

Usage (from the repository root):
    python tools/python/gen_lua.py
"""

from __future__ import annotations

from pathlib import Path

from bindgen import ir_loader
from bindgen.backends import lua_sol2, lua_typestub

REPO_ROOT = Path(__file__).resolve().parents[2]
IR_PATH = REPO_ROOT / "tools/bindings/ir/bindings.ir.json"
STUB = REPO_ROOT / "tools/lua-stubs/vultra.lua"


def main() -> None:
    ir = ir_loader.load(IR_PATH)

    areas = sorted({mod.area for mod in ir.modules}
                   | {u.area for u in ir.usertypes}
                   | {e.area for e in ir.enums}
                   | {s.area for s in ir.structs}
                   | {r.area for r in ir.raws})

    written = []
    for area in areas:
        modules = [mod for mod in ir.modules if mod.area == area]
        usertypes = [u for u in ir.usertypes if u.area == area]
        structs = [s for s in ir.structs if s.area == area]
        enums = [e for e in ir.enums if e.area == area]
        raws = [r for r in ir.raws if r.area == area]
        path = lua_sol2.out_path(REPO_ROOT, area)
        content = lua_sol2.render(area, modules, usertypes, structs, enums, raws)
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")
            written.append(path.name)

    stub_changed = lua_typestub.patch(STUB, lua_typestub.render_section(ir))

    parts = []
    if written:
        parts.append(f"sol2: {', '.join(written)}")
    if stub_changed:
        parts.append("stub updated")
    status = "; ".join(parts) if parts else "unchanged"
    print(f"[gen_lua] {len(ir.modules)} module(s), {len(ir.enums)} enum(s) -> {status}")


if __name__ == "__main__":
    main()
