#!/usr/bin/env python3
"""ImGui -> Lua binding generator (Phase 4).

Consumes dear_bindings JSON metadata generated from the engine's pinned ImGui
version and emits sol2 registration code for an allowlisted subset, plus a
LuaLS stub section. ImGui keeps upstream PascalCase names by design -- this is
the documented spec exception in doc/lua_api_design.md section 9.

Regeneration steps (after an ImGui version bump):
    git clone --depth 1 https://github.com/dearimgui/dear_bindings build/.tmp/dear_bindings
    pip install ply
    python build/.tmp/dear_bindings/dear_bindings.py -o build/.tmp/cimgui <imgui.h from the xmake package>
    python tools/python/gen_imgui_lua.py

Output (checked in):
    source/vultra/src/function/scripting/bindings/script_imgui_binding.gen.cpp
    + the IMGUI section of tools/lua-stubs/vultra.lua

Irregular functions (InputText with buffers, texture images) are hand-written
in script_imgui_binding.cpp, not generated.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
JSON_PATH = REPO_ROOT / "build/.tmp/cimgui.json"
GEN_CPP = REPO_ROOT / "source/vultra/src/function/scripting/bindings/script_imgui_binding.gen.cpp"
STUB = REPO_ROOT / "tools/lua-stubs/vultra.lua"

STUB_BEGIN = "-- <<<BEGIN GENERATED IMGUI BINDINGS (gen_imgui_lua.py) -- do not edit>>>"
STUB_END = "-- <<<END GENERATED IMGUI BINDINGS>>>"

# (json_name, lua_name); lua_name None = json name minus the ImGui_ prefix
ALLOWLIST: list[tuple[str, str | None]] = [
    # windows
    ("ImGui_Begin", None),
    ("ImGui_End", None),
    ("ImGui_BeginChild", None),
    ("ImGui_EndChild", None),
    ("ImGui_GetWindowWidth", None),
    ("ImGui_GetWindowHeight", None),
    ("ImGui_GetContentRegionAvail", None),
    ("ImGui_SetNextWindowPos", None),
    ("ImGui_SetNextWindowSize", None),
    # layout
    ("ImGui_Separator", None),
    ("ImGui_SeparatorText", None),
    ("ImGui_SameLine", None),
    ("ImGui_Spacing", None),
    ("ImGui_NewLine", None),
    ("ImGui_Indent", None),
    ("ImGui_Unindent", None),
    ("ImGui_Dummy", None),
    # text
    ("ImGui_Text", None),
    ("ImGui_TextColored", None),
    ("ImGui_TextDisabled", None),
    ("ImGui_TextWrapped", None),
    ("ImGui_BulletText", None),
    # widgets
    ("ImGui_Button", None),
    ("ImGui_SmallButton", None),
    ("ImGui_Checkbox", None),
    ("ImGui_RadioButton", None),
    ("ImGui_ProgressBar", None),
    ("ImGui_Bullet", None),
    # sliders / drags / inputs
    ("ImGui_SliderFloat", None),
    ("ImGui_SliderInt", None),
    ("ImGui_DragFloat", None),
    ("ImGui_DragInt", None),
    ("ImGui_InputFloat", None),
    ("ImGui_InputInt", None),
    # combo / selectable
    ("ImGui_BeginCombo", None),
    ("ImGui_EndCombo", None),
    ("ImGui_Selectable", None),
    # trees
    ("ImGui_TreeNode", None),
    ("ImGui_TreePop", None),
    ("ImGui_CollapsingHeader", None),
    ("ImGui_SetNextItemOpen", None),
    # menus
    ("ImGui_BeginMenuBar", None),
    ("ImGui_EndMenuBar", None),
    ("ImGui_BeginMainMenuBar", None),
    ("ImGui_EndMainMenuBar", None),
    ("ImGui_BeginMenu", None),
    ("ImGui_EndMenu", None),
    ("ImGui_MenuItem", None),
    # popups
    ("ImGui_OpenPopup", None),
    ("ImGui_BeginPopup", None),
    ("ImGui_EndPopup", None),
    ("ImGui_BeginPopupModal", None),
    ("ImGui_CloseCurrentPopup", None),
    # tooltips
    ("ImGui_BeginTooltip", None),
    ("ImGui_EndTooltip", None),
    ("ImGui_SetTooltip", None),
    ("ImGui_SetItemTooltip", None),
    # tabs
    ("ImGui_BeginTabBar", None),
    ("ImGui_EndTabBar", None),
    ("ImGui_BeginTabItem", None),
    ("ImGui_EndTabItem", None),
    # tables
    ("ImGui_BeginTable", None),
    ("ImGui_EndTable", None),
    ("ImGui_TableNextRow", None),
    ("ImGui_TableNextColumn", None),
    ("ImGui_TableSetColumnIndex", None),
    ("ImGui_TableSetupColumn", None),
    ("ImGui_TableHeadersRow", None),
    # item queries
    ("ImGui_IsItemHovered", None),
    ("ImGui_IsItemActive", None),
    ("ImGui_IsItemEdited", None),
    ("ImGui_IsItemClicked", None),
    # id stack
    ("ImGui_PushIDStr", "PushID"),
    ("ImGui_PopID", None),
    # style
    ("ImGui_PushStyleColorImVec4", "PushStyleColor"),
    ("ImGui_PopStyleColor", None),
    ("ImGui_PushStyleVar", None),
    ("ImGui_PushStyleVarImVec2", "PushStyleVarVec2"),
    ("ImGui_PopStyleVar", None),
]

# emitted as ImGui.<table> with cleaned member names
ENUMS = [
    "ImGuiWindowFlags_",
    "ImGuiChildFlags_",
    "ImGuiCond_",
    "ImGuiCol_",
    "ImGuiStyleVar_",
    "ImGuiTableFlags_",
    "ImGuiTableColumnFlags_",
    "ImGuiTableRowFlags_",
    "ImGuiSelectableFlags_",
    "ImGuiComboFlags_",
    "ImGuiTreeNodeFlags_",
    "ImGuiPopupFlags_",
    "ImGuiHoveredFlags_",
    "ImGuiInputTextFlags_",
    "ImGuiSliderFlags_",
    "ImGuiTabBarFlags_",
    "ImGuiTabItemFlags_",
]

NUMERIC = {
    "bool": "bool",
    "int": "int",
    "float": "float",
    "double": "double",
    "unsigned int": "unsigned int",
}


def fail(msg: str) -> None:
    print(f"[gen_imgui_lua] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def write_if_changed(target: Path, content: str) -> bool:
    """Write only when content differs, so an unchanged regen does not bump
    mtime and force a recompile. Returns True if the file was written."""
    if target.exists() and target.read_text(encoding="utf-8") == content:
        return False
    target.write_text(content, encoding="utf-8", newline="\n")
    return True


def builtin_of(tdesc: dict) -> str | None:
    if tdesc.get("kind") == "Builtin":
        return tdesc.get("builtin_type")
    return None


def parse_default(raw: str | None):
    return raw  # raw C literal string or None


class Arg:
    """Classified argument with C++ emission info."""

    def __init__(self, kind: str, name: str, decl: str, default: str | None):
        self.kind = kind  # str|num|enum|vec2|vec4|out|outopt|fmt
        self.name = name
        self.decl = decl  # original C declaration ("ImGuiWindowFlags", "float", ...)
        self.default = default


def classify_args(fn: dict) -> list[Arg] | None:
    args: list[Arg] = []
    raw = fn["arguments"]
    i = 0
    while i < len(raw):
        a = raw[i]
        if a.get("is_varargs"):
            fail(f"{fn['name']}: varargs not preceded by fmt string")
        name = a["name"]
        default = parse_default(a.get("default_value"))
        desc = a["type"]["description"]
        decl = a["type"]["declaration"]
        kind = desc.get("kind")

        if kind == "Builtin":
            bt = desc["builtin_type"]
            if bt not in NUMERIC:
                return None
            args.append(Arg("num", name, decl, default))
        elif kind == "User":
            uname = desc["name"]
            if uname == "ImVec2":
                args.append(Arg("vec2", name, decl, default))
            elif uname == "ImVec4":
                args.append(Arg("vec4", name, decl, default))
            else:
                # flags / ImGuiCond / ImGuiID and friends are int typedefs
                args.append(Arg("enum", name, decl, default))
        elif kind == "Pointer":
            inner = desc["inner_type"]
            ibt = inner.get("builtin_type")
            is_const = "const" in inner.get("storage_classes", [])
            if ibt == "char" and is_const:
                # fmt + varargs collapses into one string parameter
                if i + 1 < len(raw) and raw[i + 1].get("is_varargs"):
                    args.append(Arg("fmt", name, decl, None))
                    i += 2
                    continue
                args.append(Arg("str", name, decl, default))
            elif ibt in ("bool", "int", "float", "double") and not is_const:
                args.append(Arg("outopt" if default == "NULL" else "out", name, ibt, None))
            else:
                return None
        else:
            return None
        i += 1
    return args


def ret_kind(fn: dict) -> str | None:
    desc = fn["return_type"]["description"]
    if desc.get("kind") == "Builtin":
        bt = desc["builtin_type"]
        if bt == "void":
            return "void"
        if bt in NUMERIC:
            return bt
    if desc.get("kind") == "User" and desc.get("name") == "ImVec2":
        return "vec2"
    return None


def cpp_literal(default: str) -> str:
    return default


def vec_default(default: str, n: int) -> str:
    m = re.match(r"ImVec[24]\(([^)]*)\)", default)
    if not m:
        fail(f"unparsable vec default: {default}")
    return f"ImVec{n}({m.group(1)})"


def emit_function(fn: dict, lua_name: str) -> tuple[str, str]:
    """Returns (cpp_code, stub_line)."""
    original = fn["original_fully_qualified_name"]
    args = classify_args(fn)
    if args is None:
        fail(f"{fn['name']}: unsupported argument shape (hand-write it instead)")
    rk = ret_kind(fn)
    if rk is None:
        fail(f"{fn['name']}: unsupported return type")

    params: list[str] = []
    pre: list[str] = []
    call: list[str] = []
    outs: list[tuple[str, str]] = []  # (expr, luals_type)
    stub_params: list[str] = []

    for a in args:
        if a.kind == "str":
            if a.default == "NULL":
                params.append(f"sol::optional<std::string> {a.name}")
                call.append(f"{a.name} ? {a.name}->c_str() : nullptr")
                stub_params.append(f"{a.name}?: string")
            else:
                params.append(f"const char* {a.name}")
                call.append(a.name)
                stub_params.append(f"{a.name}: string")
        elif a.kind == "fmt":
            params.append("const char* text")
            call.append('"%s"')
            call.append("text")
            stub_params.append("text: string")
        elif a.kind == "num":
            ctype = NUMERIC[a.decl] if a.decl in NUMERIC else a.decl
            lt = "boolean" if ctype == "bool" else ("integer" if "int" in ctype else "number")
            if a.default is not None:
                params.append(f"sol::optional<{ctype}> {a.name}")
                call.append(f"{a.name}.value_or({cpp_literal(a.default)})")
                stub_params.append(f"{a.name}?: {lt}")
            else:
                params.append(f"{ctype} {a.name}")
                call.append(a.name)
                stub_params.append(f"{a.name}: {lt}")
        elif a.kind == "enum":
            if a.default is not None:
                params.append(f"sol::optional<lua_Integer> {a.name}")
                call.append(f"static_cast<{a.decl}>({a.name}.value_or({cpp_literal(a.default)}))")
                stub_params.append(f"{a.name}?: integer")
            else:
                params.append(f"lua_Integer {a.name}")
                call.append(f"static_cast<{a.decl}>({a.name})")
                stub_params.append(f"{a.name}: integer")
        elif a.kind in ("vec2", "vec4"):
            n = 2 if a.kind == "vec2" else 4
            script = f"ScriptVec{n}"
            fields = ["x", "y", "z", "w"][:n]
            if a.default is not None:
                params.append(f"sol::optional<{script}> {a.name}")
                make = ", ".join(f"{a.name}->{f}" for f in fields)
                call.append(f"{a.name} ? ImVec{n}({make}) : {vec_default(a.default, n)}")
                stub_params.append(f"{a.name}?: Vec{n}")
            else:
                params.append(f"const {script}& {a.name}")
                make = ", ".join(f"{a.name}.{f}" for f in fields)
                call.append(f"ImVec{n}({make})")
                stub_params.append(f"{a.name}: Vec{n}")
        elif a.kind == "out":
            ctype = a.decl
            lt = "boolean" if ctype == "bool" else ("integer" if ctype == "int" else "number")
            params.append(f"{ctype} {a.name}")
            pre.append(f"{ctype} {a.name}_io = {a.name};")
            call.append(f"&{a.name}_io")
            outs.append((f"{a.name}_io", lt))
            stub_params.append(f"{a.name}: {lt}")
        elif a.kind == "outopt":
            ctype = a.decl
            lt = "boolean" if ctype == "bool" else ("integer" if ctype == "int" else "number")
            params.append(f"sol::optional<{ctype}> {a.name}")
            pre.append(f"{ctype} {a.name}_io = {a.name}.value_or({ctype} {{}});")
            call.append(f"{a.name} ? &{a.name}_io : nullptr")
            outs.append((f"{a.name} ? sol::optional<{ctype}>({a.name}_io) : sol::optional<{ctype}>()", lt))
            stub_params.append(f"{a.name}?: {lt}")

    call_expr = f"{original}({', '.join(call)})"

    rets: list[str] = []
    stub_rets: list[str] = []
    body: list[str] = ["            detail::ensureFrame();"]
    body += [f"            {p}" for p in pre]
    if rk == "void":
        body.append(f"            {call_expr};")
    elif rk == "vec2":
        body.append(f"            const ImVec2 ret = {call_expr};")
        rets.append("ScriptVec2 {ret.x, ret.y}")
        stub_rets.append("Vec2")
    else:
        body.append(f"            const auto ret = {call_expr};")
        rets.append("ret")
        stub_rets.append("boolean" if rk == "bool" else ("integer" if "int" in rk else "number"))

    for expr, lt in outs:
        rets.append(expr)
        stub_rets.append(lt)

    if len(rets) == 1:
        body.append(f"            return {rets[0]};")
    elif len(rets) > 1:
        body.append(f"            return std::make_tuple({', '.join(rets)});")

    cpp = (f"        imgui.set_function(\"{lua_name}\", []({', '.join(params)}) {{\n"
           + "\n".join(body) + "\n        });")

    stub_sig = ", ".join(p.split(":")[0].replace("?", "") for p in stub_params)
    stub_lines = [f"---@param {p.replace(':', '').split(' ')[0]} {p.split(': ')[1]}" if False else "" for p in stub_params]
    # simpler: emit @param lines properly
    stub_lines = []
    for p in stub_params:
        pname, ptype = p.split(": ")
        opt = "?" if pname.endswith("?") else ""
        stub_lines.append(f"---@param {pname.rstrip('?')}{opt} {ptype}")
    for r in stub_rets:
        stub_lines.append(f"---@return {r}")
    stub_lines.append(f"function ImGui.{lua_name}({stub_sig}) end")
    return cpp, "\n".join(stub_lines)


def emit_enums(data: dict) -> tuple[list[str], list[str]]:
    cpp: list[str] = []
    stub: list[str] = []
    by_name = {e["name"]: e for e in data["enums"]}
    for ename in ENUMS:
        e = by_name.get(ename)
        if not e:
            fail(f"enum {ename} not found in JSON")
        table = ename[len("ImGui"):].rstrip("_")
        cpp.append(f"        {{\n            auto t = lua.create_table();")
        stub.append(f"---@field {table} table<string, integer>")
        for el in e["elements"]:
            name = el["name"]
            if "COUNT" in name or el.get("is_internal"):
                continue
            member = name[len(ename):]
            if not member:
                continue
            cpp.append(f"            t[\"{member}\"] = {el['value']};")
        cpp.append(f"            imgui[\"{table}\"] = t;\n        }}")
    return cpp, stub


def main() -> None:
    if not JSON_PATH.exists():
        fail(f"{JSON_PATH} missing; run dear_bindings first (see module docstring)")
    data = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    fns = {f["name"]: f for f in data["functions"]}

    cpp_funcs: list[str] = []
    stub_funcs: list[str] = []
    for json_name, lua_override in ALLOWLIST:
        fn = fns.get(json_name)
        if not fn:
            fail(f"allowlisted function {json_name} not found in JSON")
        lua_name = lua_override or json_name[len("ImGui_"):]
        cpp, stub = emit_function(fn, lua_name)
        cpp_funcs.append(cpp)
        stub_funcs.append(stub)

    cpp_enums, stub_enums = emit_enums(data)

    out: list[str] = []
    out.append("// GENERATED by tools/python/gen_imgui_lua.py -- DO NOT EDIT.")
    out.append("// Source: dear_bindings JSON for the engine's pinned ImGui version.")
    out.append("// ImGui.* keeps upstream PascalCase names (doc/lua_api_design.md sec. 9).")
    out.append("")
    out.append('#include "vultra/function/scripting/bindings/script_imgui_binding.hpp"')
    out.append("")
    out.append('#include "vultra/function/scripting/script_types.hpp"')
    out.append("")
    out.append("// NOLINTBEGIN")
    out.append("#include <imgui.h>")
    out.append("// NOLINTEND")
    out.append("")
    out.append("#include <string>")
    out.append("#include <tuple>")
    out.append("")
    out.append("namespace vultra")
    out.append("{")
    out.append("    namespace detail = imgui_lua_detail;")
    out.append("")
    out.append("    void registerGeneratedImGuiBindings(sol::state& lua)")
    out.append("    {")
    out.append("        auto imgui = lua.create_named_table(\"ImGui\");")
    out.append("")
    out.extend(cpp_funcs)
    out.append("")
    out.extend(cpp_enums)
    out.append("    }")
    out.append("} // namespace vultra")
    out.append("")
    cpp_changed = write_if_changed(GEN_CPP, "\n".join(out))

    stub_section = [STUB_BEGIN, ""]
    stub_section.append("--- Dear ImGui subset (editor/dev builds only; absent in headless runtimes).")
    stub_section.append("--- Calls outside the ImGui frame raise an error. Upstream PascalCase names.")
    stub_section.append("---@class ImGui")
    stub_section.extend(stub_enums)
    stub_section.append("ImGui = {}")
    stub_section.append("")
    stub_section.append("\n\n".join(stub_funcs))
    stub_section.append("")
    stub_section.append(STUB_END)

    text = STUB.read_text(encoding="utf-8")
    if STUB_BEGIN in text:
        pattern = re.compile(re.escape(STUB_BEGIN) + r".*?" + re.escape(STUB_END), re.DOTALL)
        new_text = pattern.sub("\n".join(stub_section), text)
    else:
        new_text = text.rstrip() + "\n\n" + "\n".join(stub_section) + "\n"
    stub_changed = write_if_changed(STUB, new_text)

    status = "updated" if (cpp_changed or stub_changed) else "unchanged"
    print(f"[gen_imgui_lua] {len(ALLOWLIST)} functions, {len(ENUMS)} enum tables -> {GEN_CPP.name} ({status})")


if __name__ == "__main__":
    main()
