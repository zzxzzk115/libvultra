"""LuaLS typestub backend: IR -> a managed section of tools/lua-stubs/vultra.lua.

The stub is just another backend output of the IR, so it can never drift from
the bound surface (the conformance test hard-fails on stale/missing stub
entries). This backend owns one marked section; the rest of the stub stays
hand-written until each area migrates. Today it emits enum classes (the biggest
conformance burn-down, since enum members were previously hand-duplicated).
"""

from __future__ import annotations

import re
from pathlib import Path

from .. import ir_model as m
from .. import marshallers as mar

BEGIN = "-- <<<BEGIN GENERATED (extract_bindings.py) -- do not edit>>>"
END = "-- <<<END GENERATED (extract_bindings.py)>>>"


def _enum_block(e: m.Enum) -> list[str]:
    lines = [f"--- Enum generated from {e.cpp}."]
    lines.append(f"---@class {e.luaName}")
    for v in e.values:
        lines.append(f"---@field {v.name} integer")
    lines.append(f"{e.luaName} = {{}}")
    return lines


def _module_block(mod: m.Module) -> list[str]:
    lines = [f"--- `{mod.name}` namespace (generated)."]
    lines.append(f"---@class {mod.name}")
    lines.append(f"{mod.name} = {{}}")
    for fn in mod.functions:
        if fn.callForm != "namespace":
            continue
        lines.append("")
        for p in fn.params:
            t = "any" if p.type == "raw" else mar.luals_type(p.type, p.cpp)
            lines.append(f"---@param {p.name} {t}")
        if fn.returnType not in ("void", "raw"):
            lines.append(f"---@return {mar.luals_type(fn.returnType, fn.returnCpp)}")
        params = ", ".join(p.name for p in fn.params)
        lines.append(f"function {mod.name}.{fn.luaName}({params}) end")
    return lines


def _usertype_block(ut: m.Usertype) -> list[str]:
    lines = [f"--- {ut.name} usertype (generated)."]
    lines.append(f"---@class {ut.name}")
    if ut.component:
        lines.append("---@field valid boolean @ read-only")
    for p in ut.properties:
        ro = " @ read-only" if p.readonly else ""
        lines.append(f"---@field {p.luaName} any{ro}")
    lines.append(f"local {ut.name} = {{}}")
    for fn in ut.methods:
        lines.append("")
        # skip params[0] (the implicit self handle)
        params = ", ".join(p.name for p in fn.params[1:])
        lines.append(f"function {ut.name}:{fn.luaName}({params}) end")
    return lines


def _struct_block(s: m.Struct) -> list[str]:
    lines = [f"--- Value struct generated from {s.cpp}."]
    lines.append(f"---@class {s.luaName}")
    for f in s.fields:
        ro = " @ read-only" if f.readonly else ""
        lines.append(f"---@field {f.name} {mar.luals_type(f.type, f.cpp)}{ro}")
    lines.append(f"local {s.luaName} = {{}}")
    return lines


def render_section(ir) -> str:
    lines = [BEGIN]
    for mod in sorted(ir.modules, key=lambda x: x.name):
        lines.append("")
        lines += _module_block(mod)
    for ut in sorted(ir.usertypes, key=lambda x: x.name):
        lines.append("")
        lines += _usertype_block(ut)
    for e in sorted(ir.enums, key=lambda x: x.luaName):
        lines.append("")
        lines += _enum_block(e)
    for s in sorted(ir.structs, key=lambda x: x.luaName):
        lines.append("")
        lines += _struct_block(s)
    lines.append("")
    lines.append(END)
    return "\n".join(lines)


def patch(stub_path: Path, section: str) -> bool:
    """Replace the managed section (or append it). write-if-changed."""
    text = stub_path.read_text(encoding="utf-8")
    if BEGIN in text:
        pattern = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.DOTALL)
        new_text = pattern.sub(lambda _: section, text)
    else:
        new_text = text.rstrip() + "\n\n" + section + "\n"
    if new_text == text:
        return False
    stub_path.write_text(new_text, encoding="utf-8", newline="\n")
    return True
