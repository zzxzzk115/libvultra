"""Lua/sol2 backend: IR -> script_<area>_binding.gen.cpp (one file per area).

An "area" maps to one registerScript<Area>Bindings(sol::state&, ScriptContext&)
and one .gen.cpp -- the existing hand-written registrar/header, so the generated
file is a drop-in replacement (delete the hand-written .cpp; script_binding.cpp
is unchanged). An area aggregates everything tagged with it: namespace tables
(modules), entity-ref usertypes, value structs, and enum tables.

Two function body kinds:
  serviceForward -- generator emits the full body: ctx.<svc>->method(args) with a
    service null-check (+ isValid for entity params). For trivial 1:1 forwards.
  shimCall       -- generator emits a thin forwarder to a hand-written shim that
    takes ScriptContext& first; the shim owns irregular glue.
"""

from __future__ import annotations

from pathlib import Path

from .. import ir_model as m
from .. import marshallers as mar

_BY_VALUE = {"bool", "int", "uint", "uint64", "float", "double", "enum"}


def out_path(repo_root: Path, area: str) -> Path:
    return (repo_root / "source/vultra/src/function/scripting/bindings"
            / f"script_{area}_binding.gen.cpp")


def _registrar(area: str) -> str:
    return f"registerScript{area.capitalize()}Bindings"


# ---------------------------------------------------------------- functions

def _lam_param(p: m.Param) -> str:
    cpp = mar.lua_boundary_cpp(p.type, p.cpp)
    if p.type == "raw":
        return f"{cpp} {p.name}"  # spelling already carries const&/value
    if p.type in _BY_VALUE:
        return f"{cpp} {p.name}"
    return f"const {cpp}& {p.name}"


def _arg_expr(p: m.Param) -> str:
    if p.type == "enum":
        return f"static_cast<{p.cpp}>({p.name})"
    if p.type == "entity":
        return f"{p.name}.value"
    if mar.is_vec(p.type):
        return f"fromScript({p.name})"
    if p.type == "uuid":
        return f"uuidFromString({p.name})"
    return p.name


def _return_expr(fn: m.Function, call: str) -> str:
    if mar.is_vec(fn.returnType):
        return f"toScript({call})"
    if fn.returnType == "uuid":
        return f"uuidToString({call})"
    if fn.returnType == "entity":
        return f"ScriptEntity {{{call}}}"
    if fn.returnType == "enum":
        return f"static_cast<int>({call})"
    return call


def _guard(fn: m.Function) -> str:
    parts: list[str] = []
    if fn.serviceDep:
        parts.append(f"ctx.{fn.serviceDep}")
    for p in fn.params:
        if p.type == "entity":
            parts.append(f"ctx.isValid({p.name}.value)")
    return " && ".join(parts)


def _emit_callable(fn: m.Function, indent: str) -> list[str]:
    """Emit the `[&ctx](...) { ... }` lambda body lines (no leading key)."""
    if fn.bodyKind == "shimCall":
        lam_params = ", ".join(_lam_param(p) for p in fn.params)
        names = ", ".join(p.name for p in fn.params)
        callargs = "ctx" + (", " + names if names else "")
        call = f"{fn.cppCall}({callargs})"
        head = f"{indent}[&ctx]({lam_params}) {{"
        if fn.returnType == "void":
            return [head, f"{indent}    {call};", f"{indent}}}"]
        return [head, f"{indent}    return {call};", f"{indent}}}"]

    needs_ctx = bool(fn.serviceDep) or any(p.type == "entity" for p in fn.params)
    capture = "[&ctx]" if needs_ctx else "[]"
    lam_params = ", ".join(_lam_param(p) for p in fn.params)
    args = ", ".join(_arg_expr(p) for p in fn.params)
    call = f"ctx.{fn.serviceDep}->{fn.cppCall}({args})" if fn.serviceDep else f"{fn.cppCall}({args})"
    guard = _guard(fn)
    head = f"{indent}{capture}({lam_params}) {{"
    if fn.returnType == "void":
        body = f"{indent}    if ({guard}) {call};" if guard else f"{indent}    {call};"
        return [head, body, f"{indent}}}"]
    ret = _return_expr(fn, call)
    body = f"{indent}    return {guard} ? {ret} : {fn.nullReturn};" if guard else f"{indent}    return {ret};"
    return [head, body, f"{indent}}}"]


def _emit_namespace_function(fn: m.Function) -> list[str]:
    lines = [f'        ns.set_function("{fn.luaName}",']
    lines += _emit_callable(fn, "            ")
    lines[-1] += ");"
    return lines


# ---------------------------------------------------------------- usertypes/structs

def _emit_struct(st: m.Struct) -> list[str]:
    lines = [f'        lua.new_usertype<{st.cpp}>(']
    lines.append(f'            "{st.luaName}",')
    members = []
    for f in st.fields:
        ptr = f"&{st.cpp}::{f.name}"
        if f.readonly:
            ptr = f"sol::readonly({ptr})"
        members.append(f'            "{f.name}",\n            {ptr}')
    lines.append(",\n".join(members) + ");")
    return lines


def _field_get(ut: m.Usertype, f: m.StructField) -> str:
    access = f'requireComponentRef<{ut.component}>(ctx, self.entity, "{ut.component}").{f.cpp}'
    if mar.is_vec(f.type):
        return f"toScript({access})"
    if f.type == "uuid":
        return f"uuidToString({access})"
    return access


def _field_set_stmt(ut: m.Usertype, f: m.StructField) -> str:
    access = f'requireComponentRef<{ut.component}>(ctx, self.entity, "{ut.component}").{f.cpp}'
    if mar.is_vec(f.type):
        return f"{access} = fromScript(value);"
    if f.type == "uuid":
        return f"{access} = uuidFromString(value);"
    return f"{access} = value;"


def _emit_field_property(ut: m.Usertype, f: m.StructField, lua_name: str, deprecated_of: str | None) -> str:
    value_type = mar.lua_boundary_cpp(f.type, None) if f.type != "raw" else f.cpp
    warn = ""
    if deprecated_of:
        warn = (f'\n                script_binding::warnDeprecated("{ut.name}.{lua_name}", '
                f'"{ut.name}.{deprecated_of}");')
    get = (f"[&ctx](const {ut.handle}& self) {{{warn}\n"
           f"                return {_field_get(ut, f)};\n            }}")
    if f.readonly:
        return f'            "{lua_name}",\n            script_binding::readonlyProperty({get})'
    set_ = (f"[&ctx](const {ut.handle}& self, const {value_type}& value) {{{warn}\n"
            f"                {_field_set_stmt(ut, f)}\n            }}")
    return f'            "{lua_name}",\n            script_binding::property({get},\n            {set_})'


def _emit_usertype(ut: m.Usertype) -> list[str]:
    entries: list[str] = []
    if ut.component:
        entries.append(
            '            "valid",\n'
            f"            script_binding::readonlyProperty([&ctx](const {ut.handle}& self) {{\n"
            "                auto* world = ctx.world();\n"
            f"                return world && world->registry().all_of<{ut.component}>(self.entity);\n"
            "            })")
    for f in ut.fields:
        entries.append(_emit_field_property(ut, f, f.name, None))
        if f.deprecated:
            entries.append(_emit_field_property(ut, f, f.deprecated, f.name))
    for p in ut.properties:
        getter = (f"[&ctx](const {ut.handle}& self) {{ return {p.getterShim}(ctx, self); }}")
        if p.readonly or not p.setterShim:
            entries.append(f'            "{p.luaName}",\n'
                           f"            script_binding::readonlyProperty({getter})")
        else:
            setter = (f"[&ctx](const {ut.handle}& self, {p.setterParam} value) "
                      f"{{ {p.setterShim}(ctx, self, value); }}")
            entries.append(f'            "{p.luaName}",\n'
                           f"            script_binding::property({getter},\n            {setter})")
    for fn in ut.methods:
        call = _emit_callable(fn, "            ")
        block = f'            "{fn.luaName}",\n' + "\n".join(call)
        entries.append(block)
    lhs = f"        auto {ut.handle}Type = lua.new_usertype<{ut.handle}>(" if ut.postRegister \
        else f"        lua.new_usertype<{ut.handle}>("
    out = [lhs]
    out.append(f'            "{ut.name}",')
    out.append(",\n".join(entries) + ");")
    if ut.postRegister:
        out.append(f"        {ut.postRegister}({ut.handle}Type, ctx);")
    return out


# ---------------------------------------------------------------- render

def _used_vecs(modules: list[m.Module]) -> set[str]:
    vecs: set[str] = set()
    for mod in modules:
        for fn in mod.functions:
            if mar.is_vec(fn.returnType):
                vecs.add(fn.returnType)
            for p in fn.params:
                if mar.is_vec(p.type):
                    vecs.add(p.type)
    return vecs


def render(area: str, modules: list[m.Module], usertypes: list[m.Usertype],
           structs: list[m.Struct], enums: list[m.Enum], raws: list[m.Raw] | None = None) -> str:
    raws = raws or []
    component_uts = [u for u in usertypes if u.fields]
    used_vecs = _used_vecs(modules)
    for u in usertypes:
        for f in u.fields:
            if mar.is_vec(f.type):
                used_vecs.add(f.type)
    needs_uuid = (any(fn.returnType == "uuid" or any(p.type == "uuid" for p in fn.params)
                      for mod in modules for fn in mod.functions)
                  or any(f.type == "uuid" for u in usertypes for f in u.fields))
    needs_world = any(u.component for u in usertypes)
    needs_require = bool(component_uts)
    accessor_uts = [u for u in component_uts if u.accessor]

    includes = [
        f'"vultra/function/scripting/bindings/script_{area}_binding.hpp"',
        "",
        '"vultra/function/scripting/bindings/script_binding_common.hpp"',
        '"vultra/function/scripting/script_context.hpp"',
        '"vultra/function/scripting/script_types.hpp"',
    ]
    if needs_world:
        includes.append('"vultra/function/world/world.hpp"')
    already = {inc for inc in includes if inc}
    headers = ([mod.header for mod in modules] + [u.header for u in usertypes]
               + [e.header for e in enums] + [s.header for s in structs]
               + [r.header for r in raws])
    for h in sorted(set(headers)):
        inc = f'"{h}"'
        if h and inc not in already:
            includes.append(inc)
            already.add(inc)
    if needs_uuid:
        includes.append('"vultra/core/base/uuid.hpp"')
    if accessor_uts:
        includes.append('"vultra/function/scripting/bindings/script_generated_binding.hpp"')

    out: list[str] = []
    out.append("// GENERATED by tools/python/extract_bindings.py + bindgen.backends.lua_sol2 -- DO NOT EDIT.")
    out.append("// Source of truth: tools/bindings/ir/bindings.ir.json (from VBIND_* annotations).")
    out.append("")
    for inc in includes:
        out.append(f"#include {inc}" if inc else "")
    out.append("")
    for v in sorted(used_vecs):
        out.append(f"#include <glm/vec{v[-1]}.hpp>")
    if used_vecs:
        out.append("")
    out.append("#include <cstdint>")
    if needs_require:
        out.append("#include <stdexcept>")
    out.append("#include <string>")
    out.append("")
    out.append("namespace vultra")
    out.append("{")

    helper_lines: list[str] = []
    if needs_require:
        helper_lines += [
            "        template<typename Component>",
            "        Component& requireComponentRef(ScriptContext& ctx, entt::entity entity, const char* name)",
            "        {",
            "            auto* world = ctx.world();",
            '            if (!world)',
            '                throw std::runtime_error("ScriptContext has no World");',
            "            auto* component = world->registry().try_get<Component>(entity);",
            "            if (!component)",
            '                throw std::runtime_error(std::string("Entity has no ") + name);',
            "            return *component;",
            "        }",
        ]
    for v in sorted(used_vecs):
        comps = {"vec2": "{v.x, v.y}", "vec3": "{v.x, v.y, v.z}", "vec4": "{v.x, v.y, v.z, v.w}"}[v]
        sv = {"vec2": "ScriptVec2", "vec3": "ScriptVec3", "vec4": "ScriptVec4"}[v]
        n = v[-1]
        helper_lines.append(f"        [[maybe_unused]] {sv} toScript(const glm::vec{n}& v) {{ return {comps}; }}")
        helper_lines.append(f"        [[maybe_unused]] glm::vec{n} fromScript(const {sv}& v) {{ return {comps}; }}")
    if needs_uuid:
        helper_lines += [
            "        [[maybe_unused]] std::string uuidToString(const CoreUUID& id)",
            "        {",
            "            return id.valid() ? id.toString() : std::string {};",
            "        }",
            "        [[maybe_unused]] CoreUUID uuidFromString(const std::string& s)",
            "        {",
            "            vbase::UUID uuid {};",
            "            vbase::try_parse_uuid(s.c_str(), uuid);",
            "            return CoreUUID(uuid);",
            "        }",
        ]
    if helper_lines:
        out.append("    namespace")
        out.append("    {")
        out += helper_lines
        out.append("    } // namespace")
        out.append("")

    out.append(f"    void {_registrar(area)}(sol::state& lua, ScriptContext& ctx)")
    out.append("    {")
    out.append("        (void)ctx;")
    for st in structs:
        out += _emit_struct(st)
        out.append("")
    for ut in usertypes:
        out += _emit_usertype(ut)
        out.append("")
    for mod in modules:
        out.append("        {")
        out.append(f'            auto ns = script_binding::getOrCreateTable(lua, "{mod.name}");')
        for fn in mod.functions:
            out += ["    " + ln for ln in _emit_namespace_function(fn)]
        out.append("        }")
        out.append("")
    for e in enums:
        out.append(f'        script_binding::bindEnumTable<{e.cpp}>(lua, "{e.luaName}");')
    for r in raws:
        out.append(f"        {r.symbol}(lua, ctx);")
    out.append("    }")

    # entity.<accessor> + entity:has<Name>() for component usertypes
    if accessor_uts:
        out.append("")
        out.append("    void applyGeneratedEntityAccessors(sol::usertype<ScriptEntity>& entityType, ScriptContext& ctx)")
        out.append("    {")
        for ut in accessor_uts:
            has = "has" + ut.accessor[0].upper() + ut.accessor[1:]
            out.append(f'        entityType["{ut.accessor}"] =')
            out.append(f"            sol::readonly_property([](const ScriptEntity& self) {{ return {ut.handle} {{self.value}}; }});")
            out.append(f'        entityType["{has}"] = [&ctx](const ScriptEntity& self) {{')
            out.append("            auto* world = ctx.world();")
            out.append(f"            return world && ctx.isValid(self.value) && world->registry().all_of<{ut.component}>(self.value);")
            out.append("        };")
        out.append("    }")
    out.append("} // namespace vultra")
    out.append("")
    return "\n".join(out)
