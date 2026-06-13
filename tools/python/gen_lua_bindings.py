#!/usr/bin/env python3
"""Annotation-driven Lua binding generator (Phase 2 pilot).

Parses VLUA_CLASS/VLUA_FIELD-annotated component headers (see
source/vultra/include/vultra/core/base/lua_annotations.hpp) with libclang and
emits, from one source of truth:

  * sol2 registration code:
      source/vultra/src/function/scripting/bindings/script_components_binding.gen.cpp
    (checked in -- no build-time Python dependency)
  * a LuaLS stub section in tools/lua-stubs/vultra.lua between
    "-- <<<BEGIN GENERATED" / "-- <<<END GENERATED" markers

Naming rules from doc/lua_api_design.md are enforced at generation time:
PascalCase usertype names, camelCase members, no get/set prefixes, no
Euler/Degrees suffixes (use `deprecated=` for the legacy alias instead).

Usage (from the repository root):
    pip install libclang
    python tools/python/gen_lua_bindings.py

Requires .vscode/compile_commands.json (xmake emits it on build) for include
paths and defines.
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

from clang import cindex

REPO_ROOT = Path(__file__).resolve().parents[2]

# Headers scanned for VLUA_* annotations. Extend this list as components adopt
# annotations.
ANNOTATED_HEADERS = [
    "source/vultra/include/vultra/function/world/components/audio_listener_component.hpp",
    "source/vultra/include/vultra/function/world/components/audio_source_component.hpp",
    "source/vultra/include/vultra/function/world/components/box_shape_component.hpp",
    "source/vultra/include/vultra/function/world/components/camera_component.hpp",
    "source/vultra/include/vultra/function/world/components/capsule_shape_component.hpp",
    "source/vultra/include/vultra/function/world/components/cylinder_shape_component.hpp",
    "source/vultra/include/vultra/function/world/components/environment_component.hpp",
    "source/vultra/include/vultra/function/world/components/light_component.hpp",
    "source/vultra/include/vultra/function/world/components/particle_emitter_component.hpp",
    "source/vultra/include/vultra/function/world/components/reflection_probe_component.hpp",
    "source/vultra/include/vultra/function/world/components/sphere_shape_component.hpp",
]

GEN_CPP = REPO_ROOT / "source/vultra/src/function/scripting/bindings/script_components_binding.gen.cpp"
STUB = REPO_ROOT / "tools/lua-stubs/vultra.lua"
COMPILE_COMMANDS = REPO_ROOT / ".vscode/compile_commands.json"

STUB_BEGIN = "-- <<<BEGIN GENERATED BINDINGS (gen_lua_bindings.py) -- do not edit>>>"
STUB_END = "-- <<<END GENERATED BINDINGS>>>"

PASCAL = re.compile(r"^[A-Z][A-Za-z0-9]*$")
CAMEL = re.compile(r"^[a-z][A-Za-z0-9]*$")


@dataclass
class FieldInfo:
    cpp_name: str
    lua_name: str
    type_key: str  # bool|float|int|uint|string|vec2|vec3|vec4
    readonly: bool = False
    deprecated: str | None = None  # legacy lua name kept as warn-once alias


@dataclass
class ClassInfo:
    component: str  # C++ component type
    lua_name: str   # usertype name in Lua
    ref: str        # script ref struct (has `entt::entity entity`)
    header: str     # include path of the component header
    accessor: str | None = None  # entity property name (entity.<accessor>)
    fields: list[FieldInfo] = field(default_factory=list)


TYPE_MAP = {
    "bool": "bool",
    "float": "float",
    "int": "int",
    "unsigned int": "uint",
}

LUALS_TYPES = {
    "bool": "boolean",
    "float": "number",
    "int": "integer",
    "uint": "integer",
    "string": "string",
    "vec2": "Vec2",
    "vec3": "Vec3",
    "vec4": "Vec4",
    "uuid": "string",
}

CPP_VALUE_TYPES = {
    "bool": "bool",
    "float": "float",
    "int": "int",
    "uint": "std::uint32_t",
    "string": "std::string",
    "vec2": "ScriptVec2",
    "vec3": "ScriptVec3",
    "vec4": "ScriptVec4",
    "uuid": "std::string",
}

# matched against the non-canonical type spelling before canonical classification
SPELLING_TYPES = {
    "CoreUUID": "uuid",
}


def fail(msg: str) -> None:
    print(f"[gen_lua_bindings] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def classify_type(spelling: str, canonical: str) -> str | None:
    # CoreUUID fields surface as strings in Lua (uuidToString/uuidFromString)
    if spelling.split("::")[-1] == "CoreUUID":
        return "uuid"
    if canonical in TYPE_MAP:
        return TYPE_MAP[canonical]
    if canonical.startswith("std::basic_string<char"):
        return "string"
    if canonical.startswith("glm::vec<2"):
        return "vec2"
    if canonical.startswith("glm::vec<3"):
        return "vec3"
    if canonical.startswith("glm::vec<4"):
        return "vec4"
    return None


def parse_options(payload: str) -> dict[str, str]:
    """Parse the stringified macro args: "name=fovY, deprecated=fovYDegrees"."""
    options: dict[str, str] = {}
    for part in payload.split(","):
        part = part.strip()
        if not part:
            continue
        if "=" in part:
            key, value = part.split("=", 1)
            options[key.strip()] = value.strip()
        else:
            options[part] = "true"
    return options


def clang_args() -> list[str]:
    if not COMPILE_COMMANDS.exists():
        fail(f"{COMPILE_COMMANDS} not found; build once so xmake emits it")
    db = json.loads(COMPILE_COMMANDS.read_text(encoding="utf-8"))
    entry = next((e for e in db if "script_binding" in e.get("file", "")), db[0])
    raw = entry.get("arguments") or entry.get("command", "").split()
    args = [
        "-x", "c++", "-std=c++23", "-DVULTRA_BINDGEN", "-Wno-everything",
        # pip libclang may lag behind the MSVC STL's expected clang version;
        # we only parse declarations, so the mismatch is acceptable
        "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
    ]
    for a in raw:
        if a.startswith(("/I", "-I")):
            args.append("-I" + str((REPO_ROOT / a[2:]).resolve()) if not Path(a[2:]).is_absolute() else "-I" + a[2:])
        elif a.startswith("/external:I"):
            args.append("-isystem" + a[len("/external:I"):])
        elif a.startswith(("/D", "-D")) and not a.startswith("-DVULTRA_BINDGEN"):
            args.append("-D" + a[2:])
    return args


def annotation_payload(cursor: cindex.Cursor, prefix: str) -> str | None:
    for child in cursor.get_children():
        if child.kind == cindex.CursorKind.ANNOTATE_ATTR and child.displayname.startswith(prefix):
            return child.displayname[len(prefix):]
    return None


def check_names(cls: ClassInfo) -> None:
    if not PASCAL.match(cls.lua_name):
        fail(f"{cls.component}: usertype name '{cls.lua_name}' is not PascalCase")
    for f in cls.fields:
        if not CAMEL.match(f.lua_name):
            fail(f"{cls.lua_name}.{f.lua_name}: not camelCase")
        if re.match(r"^(get|set)[A-Z]", f.lua_name):
            fail(f"{cls.lua_name}.{f.lua_name}: get/set prefix on a property (spec section 2)")
        if re.search(r"(Euler|Degrees)$", f.lua_name):
            fail(f"{cls.lua_name}.{f.lua_name}: unit-suffixed name; degrees are the default "
                 f"(use deprecated= for the legacy alias)")


def collect(header_rel: str, args: list[str]) -> list[ClassInfo]:
    header = REPO_ROOT / header_rel
    index = cindex.Index.create()
    tu = index.parse(str(header), args=args)
    hard_errors = [d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
    if hard_errors:
        for d in hard_errors[:10]:
            print(f"  {d}", file=sys.stderr)
        fail(f"clang failed to parse {header_rel}")

    classes: list[ClassInfo] = []

    def visit(cursor: cindex.Cursor) -> None:
        if cursor.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.CLASS_DECL):
            payload = annotation_payload(cursor, "vlua_class:")
            if payload is not None and cursor.location.file and Path(str(cursor.location.file)) == header:
                opts = parse_options(payload)
                if "name" not in opts or "ref" not in opts:
                    fail(f"{cursor.spelling}: VLUA_CLASS requires name= and ref=")
                include = header_rel.replace("source/vultra/include/", "")
                cls = ClassInfo(component=cursor.spelling,
                                lua_name=opts["name"],
                                ref=opts["ref"],
                                header=include,
                                accessor=opts.get("accessor"))
                for child in cursor.get_children():
                    if child.kind != cindex.CursorKind.FIELD_DECL:
                        continue
                    fpayload = annotation_payload(child, "vlua_field:")
                    if fpayload is None:
                        continue
                    fopts = parse_options(fpayload)
                    type_key = classify_type(child.type.spelling, child.type.get_canonical().spelling)
                    if type_key is None:
                        fail(f"{cursor.spelling}.{child.spelling}: unsupported type "
                             f"'{child.type.get_canonical().spelling}'")
                    cls.fields.append(FieldInfo(
                        cpp_name=child.spelling,
                        lua_name=fopts.get("name", child.spelling),
                        type_key=type_key,
                        readonly="readonly" in fopts,
                        deprecated=fopts.get("deprecated"),
                    ))
                if not cls.fields:
                    fail(f"{cursor.spelling}: VLUA_CLASS with no VLUA_FIELD members")
                check_names(cls)
                classes.append(cls)
        for child in cursor.get_children():
            visit(child)

    visit(tu.cursor)
    return classes


def getter_expr(cls: ClassInfo, f: FieldInfo) -> str:
    access = f"requireComponentRef<{cls.component}>(ctx, self.entity, \"{cls.component}\").{f.cpp_name}"
    if f.type_key in ("vec2", "vec3", "vec4"):
        return f"toScript({access})"
    if f.type_key == "uuid":
        return f"uuidToString({access})"
    return access


def setter_stmt(cls: ClassInfo, f: FieldInfo) -> str:
    access = f"requireComponentRef<{cls.component}>(ctx, self.entity, \"{cls.component}\").{f.cpp_name}"
    if f.type_key in ("vec2", "vec3", "vec4"):
        return f"{access} = fromScript(value);"
    if f.type_key == "uuid":
        return f"{access} = uuidFromString(value);"
    return f"{access} = value;"


def emit_property(cls: ClassInfo, f: FieldInfo, lua_name: str, deprecated_of: str | None) -> str:
    value_type = CPP_VALUE_TYPES[f.type_key]
    warn = ""
    if deprecated_of:
        warn = (f"\n                script_binding::warnDeprecated(\"{cls.lua_name}.{lua_name}\", "
                f"\"{cls.lua_name}.{deprecated_of}\");")
    get = (f"[&ctx](const {cls.ref}& self) {{{warn}\n"
           f"                return {getter_expr(cls, f)};\n            }}")
    if f.readonly:
        return (f"            \"{lua_name}\",\n"
                f"            VULTRA_LUA_READONLY_PROPERTY({get})")
    set_ = (f"[&ctx](const {cls.ref}& self, const {value_type}& value) {{{warn}\n"
            f"                {setter_stmt(cls, f)}\n            }}")
    return (f"            \"{lua_name}\",\n"
            f"            VULTRA_LUA_PROPERTY({get},\n            {set_})")


def emit_cpp(classes: list[ClassInfo]) -> str:
    headers = sorted({c.header for c in classes})
    out = []
    out.append("// GENERATED by tools/python/gen_lua_bindings.py -- DO NOT EDIT.")
    out.append("// Source of truth: VLUA_* annotations in the component headers below.")
    out.append("")
    needs_uuid = any(f.type_key == "uuid" for c in classes for f in c.fields)
    out.append('#include "vultra/function/scripting/bindings/script_generated_binding.hpp"')
    out.append("")
    out.append('#include "vultra/function/scripting/bindings/script_binding_common.hpp"')
    out.append('#include "vultra/function/scripting/script_types.hpp"')
    out.append('#include "vultra/function/world/world.hpp"')
    if needs_uuid:
        out.append('#include "vultra/core/base/uuid.hpp"')
    for h in headers:
        out.append(f'#include "{h}"')
    out.append("")
    out.append("#include <glm/vec2.hpp>")
    out.append("#include <glm/vec3.hpp>")
    out.append("#include <glm/vec4.hpp>")
    out.append("")
    out.append("#include <cstdint>")
    out.append("#include <stdexcept>")
    out.append("#include <string>")
    out.append("")
    out.append("namespace vultra")
    out.append("{")
    out.append("    namespace")
    out.append("    {")
    out.append("        template<typename Component>")
    out.append("        Component& requireComponentRef(ScriptContext& ctx, entt::entity entity, const char* name)")
    out.append("        {")
    out.append("            auto* world = ctx.world();")
    out.append("            if (!world)")
    out.append('                throw std::runtime_error("ScriptContext has no World");')
    out.append("            auto* component = world->registry().try_get<Component>(entity);")
    out.append("            if (!component)")
    out.append('                throw std::runtime_error(std::string("Entity has no ") + name);')
    out.append("            return *component;")
    out.append("        }")
    out.append("")
    out.append("        [[maybe_unused]] ScriptVec2 toScript(const glm::vec2& v) { return {v.x, v.y}; }")
    out.append("        [[maybe_unused]] ScriptVec3 toScript(const glm::vec3& v) { return {v.x, v.y, v.z}; }")
    out.append("        [[maybe_unused]] ScriptVec4 toScript(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }")
    out.append("        [[maybe_unused]] glm::vec2 fromScript(const ScriptVec2& v) { return {v.x, v.y}; }")
    out.append("        [[maybe_unused]] glm::vec3 fromScript(const ScriptVec3& v) { return {v.x, v.y, v.z}; }")
    out.append("        [[maybe_unused]] glm::vec4 fromScript(const ScriptVec4& v) { return {v.x, v.y, v.z, v.w}; }")
    if needs_uuid:
        out.append("")
        out.append("        [[maybe_unused]] std::string uuidToString(const CoreUUID& id)")
        out.append("        {")
        out.append("            return id.valid() ? id.toString() : std::string {};")
        out.append("        }")
        out.append("        [[maybe_unused]] CoreUUID uuidFromString(const std::string& s)")
        out.append("        {")
        out.append("            vbase::UUID uuid {};")
        out.append("            vbase::try_parse_uuid(s.c_str(), uuid);")
        out.append("            return CoreUUID(uuid);")
        out.append("        }")
    out.append("    } // namespace")
    out.append("")
    out.append("    void registerGeneratedComponentBindings(sol::state& lua, ScriptContext& ctx)")
    out.append("    {")
    for cls in classes:
        props = []
        props.append("            \"valid\",\n"
                      f"            VULTRA_LUA_READONLY_PROPERTY([&ctx](const {cls.ref}& self) {{\n"
                      f"                auto* world = ctx.world();\n"
                      f"                return world && world->registry().all_of<{cls.component}>(self.entity);\n"
                      f"            }})")
        for f in cls.fields:
            props.append(emit_property(cls, f, f.lua_name, None))
            if f.deprecated:
                props.append(emit_property(cls, f, f.deprecated, f.lua_name))
        joined = ",\n".join(props)
        out.append(f"        lua.new_usertype<{cls.ref}>(")
        out.append(f"            \"{cls.lua_name}\",")
        out.append(joined + ");")
        out.append("")
    out.append("    }")
    out.append("")

    # entity.<accessor> properties + entity:has<Name>() queries
    accessors = [c for c in classes if c.accessor]
    out.append("    void applyGeneratedEntityAccessors(sol::usertype<ScriptEntity>& entityType, ScriptContext& ctx)")
    out.append("    {")
    if not accessors:
        out.append("        (void)entityType;")
        out.append("        (void)ctx;")
    for cls in accessors:
        has_name = "has" + cls.accessor[0].upper() + cls.accessor[1:]
        out.append(f"        entityType[\"{cls.accessor}\"] =")
        out.append(f"            sol::readonly_property([](const ScriptEntity& self) {{ return {cls.ref} {{self.value}}; }});")
        out.append(f"        entityType[\"{has_name}\"] = [&ctx](const ScriptEntity& self) {{")
        out.append("            auto* world = ctx.world();")
        out.append(f"            return world && ctx.isValid(self.value) && world->registry().all_of<{cls.component}>(self.value);")
        out.append("        };")
    out.append("    }")
    out.append("} // namespace vultra")
    out.append("")
    return "\n".join(out)


def emit_stub(classes: list[ClassInfo]) -> str:
    lines = [STUB_BEGIN]
    for cls in classes:
        lines.append("")
        lines.append(f"--- Component reference generated from {cls.component}.")
        lines.append(f"---@class {cls.lua_name}")
        lines.append("---@field valid boolean @ read-only")
        for f in cls.fields:
            ro = " @ read-only" if f.readonly else ""
            lines.append(f"---@field {f.lua_name} {LUALS_TYPES[f.type_key]}{ro}")
            if f.deprecated:
                lines.append(f"---@field {f.deprecated} {LUALS_TYPES[f.type_key]} @ deprecated, use {f.lua_name}")
        lines.append(f"local {cls.lua_name} = {{}}")
    lines.append("")
    lines.append(STUB_END)
    return "\n".join(lines)


def write_if_changed(target: Path, content: str) -> bool:
    """Write only when content differs, so unchanged regen does not bump mtime
    and trigger a needless recompile. Returns True if the file was written."""
    if target.exists() and target.read_text(encoding="utf-8") == content:
        return False
    target.write_text(content, encoding="utf-8", newline="\n")
    return True


def patch_stub(section: str) -> bool:
    text = STUB.read_text(encoding="utf-8")
    if STUB_BEGIN in text:
        pattern = re.compile(re.escape(STUB_BEGIN) + r".*?" + re.escape(STUB_END), re.DOTALL)
        new_text = pattern.sub(section, text)
    else:
        new_text = text.rstrip() + "\n\n" + section + "\n"
    return write_if_changed(STUB, new_text)


def main() -> None:
    args = clang_args()
    classes: list[ClassInfo] = []
    for header in ANNOTATED_HEADERS:
        classes.extend(collect(header, args))
    if not classes:
        fail("no VLUA_CLASS annotations found")
    cpp_changed = write_if_changed(GEN_CPP, emit_cpp(classes))
    stub_changed = patch_stub(emit_stub(classes))
    total_fields = sum(len(c.fields) for c in classes)
    status = "updated" if (cpp_changed or stub_changed) else "unchanged"
    print(f"[gen_lua_bindings] {len(classes)} class(es), {total_fields} field(s) -> {GEN_CPP.name} ({status})")


if __name__ == "__main__":
    main()
