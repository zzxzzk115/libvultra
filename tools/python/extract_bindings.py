#!/usr/bin/env python3
"""Stage 1: VBIND_*-annotated C++ headers -> language-neutral IR.

Reads the header manifest (tools/bindings/headers.json), parses each header with
libclang (-DVULTRA_BINDGEN), and writes the IR to
tools/bindings/ir/bindings.ir.json. Backends (Stage 2, gen_lua.py) consume that
IR; this step never emits language-specific code.

Usage (from the repository root):
    python tools/python/extract_bindings.py

Requires .vscode/compile_commands.json (xmake emits it on build) for include
paths and defines. The IR is checked in, so builds never depend on this step.
"""

from __future__ import annotations

import json
from pathlib import Path

from bindgen import SCHEMA_VERSION
from bindgen import ir_loader
from bindgen import ir_model as m
from bindgen.clang_frontend import Collected, clang_args, collect
from bindgen.naming import fail

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "tools/bindings/headers.json"
IR_PATH = REPO_ROOT / "tools/bindings/ir/bindings.ir.json"
COMPILE_COMMANDS = REPO_ROOT / ".vscode/compile_commands.json"


def main() -> None:
    if not MANIFEST.exists():
        fail(f"{MANIFEST} not found")
    headers = json.loads(MANIFEST.read_text(encoding="utf-8")).get("headers", [])
    if not headers:
        fail("header manifest is empty")

    args = clang_args(REPO_ROOT, COMPILE_COMMANDS)
    out = Collected()
    for header in headers:
        collect(header, REPO_ROOT, args, out)

    # attach free shim functions to their module (declared in any header)
    by_module = {mod.name: mod for mod in out.modules}
    for module_name, fn in out.free_functions:
        mod = by_module.get(module_name)
        if mod is None:
            fail(f"shim {fn.cppCall}: module '{module_name}' not found (needs a VBIND_MODULE)")
        mod.functions.append(fn)
    for mod in out.modules:
        if not mod.functions:
            fail(f"{mod.name}: module has no functions (no VBIND_FN methods or shims)")

    # attach usertype methods/properties
    by_usertype = {u.name: u for u in out.usertypes}
    for ut_name, fn in out.free_methods:
        ut = by_usertype.get(ut_name)
        if ut is None:
            fail(f"method {fn.cppCall}: usertype '{ut_name}' not found (needs a VBIND_USERTYPE)")
        ut.methods.append(fn)
    for ut_name, prop in out.free_properties:
        ut = by_usertype.get(ut_name)
        if ut is None:
            fail(f"property {prop.getterShim}: usertype '{ut_name}' not found (needs a VBIND_USERTYPE)")
        ut.properties.append(prop)

    # resolve the registrar area for enums/structs declared with module=
    area_of_module = {mod.name: mod.area for mod in out.modules}
    for e in out.enums:
        e.area = area_of_module.get(e.module, e.module.lower()) if e.module else (e.area or e.luaName.lower())
    for s in out.structs:
        s.area = area_of_module.get(s.module, s.module.lower()) if s.module else (s.area or s.luaName.lower())

    ir = m.IR(schemaVersion=SCHEMA_VERSION,
              modules=out.modules, usertypes=out.usertypes, enums=out.enums,
              structs=out.structs, raws=out.raws)
    changed = ir_loader.dump(ir, IR_PATH)
    fn_count = sum(len(mod.functions) for mod in ir.modules)
    status = "updated" if changed else "unchanged"
    print(f"[extract_bindings] {len(ir.modules)} module(s), {fn_count} function(s), "
          f"{len(ir.enums)} enum(s) -> {IR_PATH.name} ({status})")


if __name__ == "__main__":
    main()
