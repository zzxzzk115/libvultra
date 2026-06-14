"""libclang frontend: annotated C++ headers -> typed IR fragments.

Parses headers compiled with -DVULTRA_BINDGEN (so VBIND_* macros become
[[clang::annotate("vbind_<kind>:...")]]) and turns the annotated declarations
into bindgen.ir_model objects. Compiler flags/include paths come from
.vscode/compile_commands.json (xmake emits it after a build).

This module is the ONLY place that touches libclang; backends never see clang.
"""

from __future__ import annotations

import json
from pathlib import Path

from clang import cindex

from . import ir_model as m
from .naming import check_camel, check_fn_name, check_pascal, check_struct_field, fail, normalize_enum_name


# ---------------------------------------------------------------- clang setup

def clang_args(repo_root: Path, compile_commands: Path) -> list[str]:
    if not compile_commands.exists():
        fail(f"{compile_commands} not found; build once so xmake emits it")
    db = json.loads(compile_commands.read_text(encoding="utf-8"))
    entry = next((e for e in db if "script_binding" in e.get("file", "")), db[0])
    raw = entry.get("arguments") or entry.get("command", "").split()
    args = [
        "-x", "c++", "-std=c++23", "-DVULTRA_BINDGEN", "-Wno-everything",
        # pip libclang may lag the MSVC STL's expected clang version; we only
        # parse declarations, so the mismatch is acceptable.
        "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
    ]
    for a in raw:
        if a.startswith(("/I", "-I")):
            inc = a[2:]
            args.append("-I" + (str((repo_root / inc).resolve()) if not Path(inc).is_absolute() else inc))
        elif a.startswith("/external:I"):
            args.append("-isystem" + a[len("/external:I"):])
        elif a.startswith(("/D", "-D")) and not a.startswith("-DVULTRA_BINDGEN"):
            args.append("-D" + a[2:])
    return args


def _annotation(cursor: cindex.Cursor, prefix: str) -> str | None:
    for child in cursor.get_children():
        if child.kind == cindex.CursorKind.ANNOTATE_ATTR and child.displayname.startswith(prefix):
            return child.displayname[len(prefix):]
    return None


def _parse_options(payload: str) -> dict[str, str]:
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


# ---------------------------------------------------------------- type mapping

_CANON_SCALARS = {
    "bool": "bool",
    "int": "int",
    "unsigned int": "uint",
    "unsigned long long": "uint64",
    "unsigned __int64": "uint64",
    "float": "float",
    "double": "double",
}


def _classify_type(t: cindex.Type) -> tuple[str, str | None]:
    """Return (token, cpp) for a clang type, or fail on unsupported types."""
    canonical = t.get_canonical()
    spelling = canonical.spelling
    # strip cv/ref noise for matching
    bare = spelling.replace("const ", "").replace("&", "").strip()

    # entt::entity is itself an enum, so match it before the generic enum case
    if bare in ("entt::entity",) or bare.endswith("::entity"):
        return "entity", None
    # script value types (struct fields hold these directly)
    tail = bare.split("::")[-1]
    if tail == "ScriptEntity":
        return "entity", None
    if tail in ("ScriptVec2", "ScriptVec3", "ScriptVec4"):
        return "vec" + tail[-1], None
    if bare.split("::")[-1] == "CoreUUID":
        return "uuid", None
    if canonical.kind == cindex.TypeKind.ENUM:
        return "enum", bare
    if canonical.kind == cindex.TypeKind.VOID:
        return "void", None
    if bare in _CANON_SCALARS:
        return _CANON_SCALARS[bare], None
    if bare.startswith("std::basic_string<char") or bare.startswith("std::basic_string_view<char"):
        return "string", None
    if bare.startswith("glm::vec<2"):
        return "vec2", None
    if bare.startswith("glm::vec<3"):
        return "vec3", None
    if bare.startswith("glm::vec<4"):
        return "vec4", None
    return "", spelling  # caller reports the unsupported spelling


# ---------------------------------------------------------------- collection

class Collected:
    def __init__(self) -> None:
        self.modules: list[m.Module] = []
        self.usertypes: list[m.Usertype] = []
        self.enums: list[m.Enum] = []
        self.structs: list[m.Struct] = []
        # free shim functions: (target module name, Function); attached to their
        # module after all headers are parsed (the module may live elsewhere).
        self.free_functions: list[tuple[str, m.Function]] = []
        # usertype methods (usertype=) and properties (VBIND_PROPERTY usertype=)
        self.free_methods: list[tuple[str, m.Function]] = []
        self.free_properties: list[tuple[str, m.UProperty]] = []
        self.raws: list[m.Raw] = []


def _include_path(header_abs: Path, repo_root: Path) -> str:
    rel = header_abs.resolve()
    inc_root = (repo_root / "source" / "vultra" / "include").resolve()
    try:
        return str(rel.relative_to(inc_root)).replace("\\", "/")
    except ValueError:
        return str(rel.relative_to(repo_root.resolve())).replace("\\", "/")


def _collect_module(cursor: cindex.Cursor, header_rel: str, header_inc: str,
                    header_abs: Path, repo_root: Path, out: Collected) -> None:
    opts = _parse_options(_annotation(cursor, "vbind_module:"))
    if "name" not in opts:
        fail(f"{cursor.spelling}: VBIND_MODULE requires name=")
    check_pascal("module", opts["name"])
    mod = m.Module(
        name=opts["name"],
        area=opts.get("area", opts["name"].lower()),
        serviceDep=opts.get("service"),
        header=header_inc,
        source=m.Source(file=header_rel, line=cursor.location.line),
    )
    for child in cursor.get_children():
        if child.kind != cindex.CursorKind.CXX_METHOD:
            continue
        payload = _annotation(child, "vbind_fn:")
        if payload is None:
            continue
        fopts = _parse_options(payload)
        lua_name = fopts.get("name", child.spelling)
        is_method = "self" in fopts
        if not is_method:
            check_fn_name(mod.name, lua_name)

        params: list[m.Param] = []
        for arg in child.get_arguments():
            tok, cpp = _classify_type(arg.type)
            if not tok:
                fail(f"{mod.name}.{lua_name}: unsupported parameter type '{cpp}'")
            params.append(m.Param(name=arg.spelling or f"arg{len(params)}", type=tok, cpp=cpp))

        rtok, rcpp = _classify_type(child.result_type)
        if not rtok:
            fail(f"{mod.name}.{lua_name}: unsupported return type '{rcpp}'")

        null_return = fopts.get("null")
        from .marshallers import default_null_return
        if null_return is None and rtok != "void":
            null_return = default_null_return(rtok, rcpp)

        mod.functions.append(m.Function(
            luaName=lua_name,
            callForm="method" if is_method else "namespace",
            bodyKind="serviceForward",
            cppCall=child.spelling,
            params=params,
            returnType=rtok,
            returnCpp=rcpp,
            serviceDep=fopts.get("service", mod.serviceDep),
            nullReturn=null_return,
            selfHandle=fopts.get("self"),
            overloadGroup=fopts.get("overload"),
            deprecatedAlias=fopts.get("deprecated"),
            source=m.Source(file=header_rel, line=child.location.line),
        ))
    out.modules.append(mod)


def _collect_free_function(cursor: cindex.Cursor, header_rel: str, out: Collected) -> None:
    """A free shim function whose first parameter is ScriptContext&. The
    generator emits a thin forwarder passing ctx + the remaining (raw,
    verbatim-typed) params; the hand-written body owns the glue. Either:
      VBIND_FN(module=X)   -> namespace function  X.<name>
      VBIND_FN(usertype=X) -> usertype method     X:<name> (params start with self)"""
    fopts = _parse_options(_annotation(cursor, "vbind_fn:"))
    module = fopts.get("module")
    usertype = fopts.get("usertype")
    if not module and not usertype:
        fail(f"{cursor.spelling}: free VBIND_FN requires module= or usertype=")
    owner = module or usertype
    lua_name = fopts.get("name", cursor.spelling)
    if module:
        check_fn_name(owner, lua_name)
    else:  # usertype method: instance members aren't conformance-checked
        check_struct_field(owner, lua_name)

    args = list(cursor.get_arguments())
    if not args or "ScriptContext" not in args[0].type.spelling:
        fail(f"{owner}.{lua_name}: shim's first parameter must be ScriptContext&")
    # remaining params are passed verbatim (for a method, the first is the handle)
    params = [m.Param(name=a.spelling or f"arg{i}", type="raw", cpp=a.type.spelling)
              for i, a in enumerate(args[1:])]
    rt = "void" if cursor.result_type.get_canonical().kind == cindex.TypeKind.VOID else "raw"

    fn = m.Function(
        luaName=lua_name,
        callForm="method" if usertype else "namespace",
        bodyKind="shimCall",
        cppCall=cursor.spelling,
        params=params,
        returnType=rt,
        returnCpp=None if rt == "void" else cursor.result_type.spelling,
        selfHandle=fopts.get("self"),
        overloadGroup=fopts.get("overload"),
        deprecatedAlias=fopts.get("deprecated"),
        source=m.Source(file=header_rel, line=cursor.location.line),
    )
    if usertype:
        out.free_methods.append((usertype, fn))
    else:
        out.free_functions.append((module, fn))


def _collect_free_property(cursor: cindex.Cursor, header_rel: str, out: Collected) -> None:
    """VBIND_PROPERTY(usertype=X, name=, set=) on a getter shim:
       T getterShim(ScriptContext& ctx, const Handle& self)
    set= names a setter shim void setShim(ScriptContext&, const Handle&, const T&)."""
    fopts = _parse_options(_annotation(cursor, "vbind_property:"))
    usertype = fopts.get("usertype")
    if not usertype:
        fail(f"{cursor.spelling}: VBIND_PROPERTY requires usertype=")
    lua_name = fopts.get("name", cursor.spelling)
    # usertype instance members are not enumerated by the conformance checker;
    # camelCase only (deprecated aliases like rotationEuler keep their suffix)
    check_struct_field(usertype, lua_name)
    setter = fopts.get("set")
    prop = m.UProperty(
        luaName=lua_name,
        getterShim=cursor.spelling,
        setterShim=setter,
        setterParam=cursor.result_type.spelling,
        readonly=setter is None,
    )
    out.free_properties.append((usertype, prop))


def _collect_usertype(cursor: cindex.Cursor, header_rel: str, header_inc: str, out: Collected) -> None:
    """VBIND_USERTYPE marker (a tag struct) declaring an entity-ref usertype.
    Methods/properties are free shims tagged usertype=<name>, attached later."""
    opts = _parse_options(_annotation(cursor, "vbind_usertype:"))
    field_prefix = "vbind_field:"
    if "name" not in opts or "handle" not in opts:
        fail(f"{cursor.spelling}: VBIND_USERTYPE requires name= and handle=")
    check_pascal("usertype", opts["name"])

    # Pure-data component fields on the annotated struct (which IS the component).
    # The generator emits requireComponentRef<component> get/set (no shim).
    fields: list[m.StructField] = []
    for child in cursor.get_children():
        if child.kind != cindex.CursorKind.FIELD_DECL:
            continue
        fpayload = _annotation(child, field_prefix)
        if fpayload is None:
            continue
        fopts = _parse_options(fpayload)
        lua_name = fopts.get("name", child.spelling)
        check_camel(opts["name"], lua_name)
        tok, cpp = _classify_type(child.type)
        if not tok:
            fail(f"{opts['name']}.{lua_name}: unsupported field type '{cpp}'")
        fields.append(m.StructField(name=lua_name, type=tok, cpp=child.spelling,
                                    readonly="readonly" in fopts, deprecated=fopts.get("deprecated")))

    # when fields are present the annotated struct is the component itself, and
    # all components share the one `components` area/registrar
    component = opts.get("component", cursor.spelling if fields else None)
    default_area = "components" if fields else opts["name"].lower()
    out.usertypes.append(m.Usertype(
        name=opts["name"],
        handle=opts["handle"],
        area=opts.get("area", default_area),
        header=header_inc,
        component=component,
        accessor=opts.get("accessor"),
        postRegister=opts.get("postRegister"),
        fields=fields,
        componentHeader=header_inc if fields else "",
        source=m.Source(file=header_rel, line=cursor.location.line),
    ))


def _collect_enum(cursor: cindex.Cursor, header_rel: str, header_inc: str, out: Collected) -> None:
    opts = _parse_options(_annotation(cursor, "vbind_enum:"))
    if "name" not in opts:
        fail(f"{cursor.spelling}: VBIND_ENUM requires name=")
    check_pascal("enum", opts["name"])
    strip_e = "stripE" in opts
    en = m.Enum(
        luaName=opts["name"],
        cpp=cursor.type.get_canonical().spelling,
        header=header_inc,
        stripE=strip_e,
        module=opts.get("module"),
        source=m.Source(file=header_rel, line=cursor.location.line),
    )
    for child in cursor.get_children():
        if child.kind == cindex.CursorKind.ENUM_CONSTANT_DECL:
            en.values.append(m.EnumValue(
                name=normalize_enum_name(child.spelling, strip_e),
                value=child.enum_value,
            ))
    if not en.values:
        fail(f"{en.luaName}: VBIND_ENUM with no enumerators")
    out.enums.append(en)


def _collect_struct(cursor: cindex.Cursor, header_rel: str, header_inc: str, out: Collected) -> None:
    """A plain value struct (VBIND_STRUCT + VBIND_FIELD members) bound as a sol2
    usertype exposing its fields. Not entity-backed (cf. component usertypes)."""
    opts = _parse_options(_annotation(cursor, "vbind_struct:"))
    if "name" not in opts:
        fail(f"{cursor.spelling}: VBIND_STRUCT requires name=")
    check_pascal("struct", opts["name"])
    all_fields = "allFields" in opts  # bind every public field (no per-field VBIND_FIELD)
    st = m.Struct(
        luaName=opts["name"],
        cpp=cursor.spelling,
        header=header_inc,
        module=opts.get("module"),
        source=m.Source(file=header_rel, line=cursor.location.line),
    )
    for child in cursor.get_children():
        if child.kind != cindex.CursorKind.FIELD_DECL:
            continue
        payload = _annotation(child, "vbind_field:")
        if payload is None and not all_fields:
            continue
        fopts = _parse_options(payload) if payload is not None else {}
        # value-struct fields keep their C++ name (used directly as the sol2
        # member pointer); the spec's camelCase rule still applies.
        lua_name = child.spelling
        check_struct_field(st.luaName, lua_name)
        tok, cpp = _classify_type(child.type)
        if not tok:
            fail(f"{st.luaName}.{lua_name}: unsupported field type '{cpp}'")
        st.fields.append(m.StructField(
            name=lua_name, type=tok, cpp=cpp, readonly="readonly" in fopts))
    if not st.fields:
        fail(f"{st.luaName}: VBIND_STRUCT with no VBIND_FIELD members")
    out.structs.append(st)


def collect(header_rel: str, repo_root: Path, args: list[str], out: Collected) -> None:
    header_abs = repo_root / header_rel
    header_inc = _include_path(header_abs, repo_root)
    index = cindex.Index.create()
    tu = index.parse(str(header_abs), args=args)
    hard = [d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
    if hard:
        for d in hard[:10]:
            print(f"  {d}")
        fail(f"clang failed to parse {header_rel}")

    def visit(cursor: cindex.Cursor) -> None:
        same_file = cursor.location.file and Path(str(cursor.location.file)) == header_abs
        if same_file and cursor.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.CLASS_DECL):
            if _annotation(cursor, "vbind_module:") is not None:
                _collect_module(cursor, header_rel, header_inc, header_abs, repo_root, out)
            elif _annotation(cursor, "vbind_usertype:") is not None:
                _collect_usertype(cursor, header_rel, header_inc, out)
            elif _annotation(cursor, "vbind_struct:") is not None:
                _collect_struct(cursor, header_rel, header_inc, out)
        if same_file and cursor.kind == cindex.CursorKind.ENUM_DECL:
            if _annotation(cursor, "vbind_enum:") is not None:
                _collect_enum(cursor, header_rel, header_inc, out)
        if same_file and cursor.kind == cindex.CursorKind.FUNCTION_DECL:
            if _annotation(cursor, "vbind_fn:") is not None:
                _collect_free_function(cursor, header_rel, out)
            elif _annotation(cursor, "vbind_property:") is not None:
                _collect_free_property(cursor, header_rel, out)
            elif (payload := _annotation(cursor, "vbind_raw:")) is not None:
                ropts = _parse_options(payload)
                out.raws.append(m.Raw(area=ropts.get("area", ""), symbol=cursor.spelling,
                                      header=header_inc, source=m.Source(file=header_rel, line=cursor.location.line)))
        for child in cursor.get_children():
            visit(child)

    visit(tu.cursor)
