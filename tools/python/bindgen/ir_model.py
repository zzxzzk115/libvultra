"""Typed IR model + (de)serialization.

These dataclasses mirror tools/bindings/ir/bindings.ir.schema.json. The IR is
plain JSON on disk; this module is the in-memory contract shared by the
extractor (which builds it) and every backend (which consumes it).

Type tokens (the `type` field on params/returns/fields) are language-neutral:
  bool int uint float double string
  vec2 vec3 vec4   -- glm::vecN <-> ScriptVecN
  uuid             -- CoreUUID  <-> string
  entity           -- entt::entity <-> ScriptEntity
  enum             -- carries cpp (e.g. "vultra::KeyCode"); Lua side is an int
  struct           -- carries cpp (a value struct bound elsewhere)
Each backend maps a token to its own realization (see bindgen.marshallers).
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field


@dataclass
class Source:
    file: str = ""  # repo-relative include path
    line: int = 0


@dataclass
class TypeRef:
    type: str  # a type token (see module docstring)
    cpp: str | None = None  # concrete C++ type for enum/struct tokens


@dataclass
class Param:
    name: str
    type: str
    cpp: str | None = None


@dataclass
class Function:
    luaName: str
    callForm: str  # "namespace" | "method"
    # serviceForward: cppCall is a method invoked on the service pointer.
    # shimCall: cppCall is a free function the generator calls (hand-written body).
    bodyKind: str  # "serviceForward" | "shimCall"
    cppCall: str  # method name (serviceForward) or qualified shim symbol (shimCall)
    params: list[Param] = field(default_factory=list)
    returnType: str = "void"
    returnCpp: str | None = None
    serviceDep: str | None = None
    nullReturn: str | None = None  # expression returned when the service is null
    selfHandle: str | None = None  # ref struct for callForm == "method"
    overloadGroup: str | None = None
    deprecatedAlias: str | None = None
    source: Source = field(default_factory=Source)


@dataclass
class Property:
    luaName: str
    type: str
    cpp: str | None = None
    readonly: bool = False
    deprecatedAlias: str | None = None


@dataclass
class Module:
    name: str  # Lua namespace table name (PascalCase)
    area: str = ""  # file/registrar stem (script_<area>_binding); defaults to name.lower()
    serviceDep: str | None = None  # ScriptContext pointer member, null-checked
    header: str = ""  # repo-relative include for the annotated declaration
    functions: list[Function] = field(default_factory=list)
    properties: list[Property] = field(default_factory=list)
    source: Source = field(default_factory=Source)


@dataclass
class UProperty:
    luaName: str
    getterShim: str  # qualified shim: T get(ScriptContext&, const Handle&)
    setterShim: str | None = None  # void set(ScriptContext&, const Handle&, const T&)
    setterParam: str = ""  # raw C++ type of the setter value (lambda param)
    readonly: bool = False


@dataclass
class Usertype:
    name: str  # Lua usertype name (PascalCase)
    handle: str  # ref struct from script_types.hpp (e.g. ScriptAnimatorRef)
    area: str = ""  # area whose registrar binds it
    header: str = ""
    component: str | None = None  # for `valid` + entity:has<Name>()
    accessor: str | None = None  # entity.<accessor>
    postRegister: str | None = None  # fn(usertype&, ctx) called after registration
    methods: list[Function] = field(default_factory=list)  # callForm=method shims
    properties: list[UProperty] = field(default_factory=list)  # shim-backed get/set
    # pure-data component fields (VBIND_FIELD on the component struct): the
    # generator emits requireComponentRef<component> get/set directly (no shim).
    fields: list[StructField] = field(default_factory=list)
    componentHeader: str = ""  # include for the component type (requireComponentRef)
    source: Source = field(default_factory=Source)


@dataclass
class EnumValue:
    name: str  # normalized Lua name (leading-e stripped when stripE)
    value: int


@dataclass
class Enum:
    luaName: str
    cpp: str  # fully-qualified C++ enum type
    header: str = ""
    stripE: bool = False
    module: str | None = None  # module whose area binds this enum table
    area: str = ""  # resolved registrar area
    values: list[EnumValue] = field(default_factory=list)
    source: Source = field(default_factory=Source)


@dataclass
class StructField:
    name: str
    type: str
    cpp: str | None = None
    readonly: bool = False
    deprecated: str | None = None  # legacy lua name kept as a warn-once alias


@dataclass
class Struct:
    luaName: str
    cpp: str  # value struct type (e.g. ScriptPhysicsRaycastHit)
    header: str = ""
    module: str | None = None  # module whose area binds this usertype
    area: str = ""  # resolved registrar area
    fields: list[StructField] = field(default_factory=list)
    source: Source = field(default_factory=Source)


@dataclass
class Raw:
    area: str
    symbol: str  # void f(sol::state&, ScriptContext&) called by the area registrar
    header: str = ""
    source: Source = field(default_factory=Source)


@dataclass
class IR:
    schemaVersion: int
    generator: str = "extract_bindings.py"
    modules: list[Module] = field(default_factory=list)
    usertypes: list[Usertype] = field(default_factory=list)
    enums: list[Enum] = field(default_factory=list)
    structs: list[Struct] = field(default_factory=list)
    raws: list[Raw] = field(default_factory=list)

    def to_json_dict(self) -> dict:
        return asdict(self)


# ---------------------------------------------------------------- from-JSON

def _params(raw: list[dict]) -> list[Param]:
    return [Param(name=p["name"], type=p["type"], cpp=p.get("cpp")) for p in raw]


def _function(raw: dict) -> Function:
    return Function(
        luaName=raw["luaName"],
        callForm=raw["callForm"],
        bodyKind=raw["bodyKind"],
        cppCall=raw["cppCall"],
        params=_params(raw.get("params", [])),
        returnType=raw.get("returnType", "void"),
        returnCpp=raw.get("returnCpp"),
        serviceDep=raw.get("serviceDep"),
        nullReturn=raw.get("nullReturn"),
        selfHandle=raw.get("selfHandle"),
        overloadGroup=raw.get("overloadGroup"),
        deprecatedAlias=raw.get("deprecatedAlias"),
        source=Source(**raw.get("source", {})),
    )


def _module(raw: dict) -> Module:
    return Module(
        name=raw["name"],
        area=raw.get("area", ""),
        serviceDep=raw.get("serviceDep"),
        header=raw.get("header", ""),
        functions=[_function(f) for f in raw.get("functions", [])],
        properties=[Property(**p) for p in raw.get("properties", [])],
        source=Source(**raw.get("source", {})),
    )


def _enum(raw: dict) -> Enum:
    return Enum(
        luaName=raw["luaName"],
        cpp=raw["cpp"],
        header=raw.get("header", ""),
        stripE=raw.get("stripE", False),
        module=raw.get("module"),
        area=raw.get("area", ""),
        values=[EnumValue(**v) for v in raw.get("values", [])],
        source=Source(**raw.get("source", {})),
    )


def _usertype(raw: dict) -> Usertype:
    return Usertype(
        name=raw["name"],
        handle=raw["handle"],
        area=raw.get("area", ""),
        header=raw.get("header", ""),
        component=raw.get("component"),
        accessor=raw.get("accessor"),
        postRegister=raw.get("postRegister"),
        methods=[_function(f) for f in raw.get("methods", [])],
        properties=[UProperty(**p) for p in raw.get("properties", [])],
        fields=[StructField(**f) for f in raw.get("fields", [])],
        componentHeader=raw.get("componentHeader", ""),
        source=Source(**raw.get("source", {})),
    )


def _struct(raw: dict) -> Struct:
    return Struct(
        luaName=raw["luaName"],
        cpp=raw["cpp"],
        header=raw.get("header", ""),
        module=raw.get("module"),
        area=raw.get("area", ""),
        fields=[StructField(**f) for f in raw.get("fields", [])],
        source=Source(**raw.get("source", {})),
    )


def from_json_dict(raw: dict) -> IR:
    return IR(
        schemaVersion=raw["schemaVersion"],
        generator=raw.get("generator", "extract_bindings.py"),
        modules=[_module(m) for m in raw.get("modules", [])],
        usertypes=[_usertype(u) for u in raw.get("usertypes", [])],
        enums=[_enum(e) for e in raw.get("enums", [])],
        structs=[_struct(s) for s in raw.get("structs", [])],
        raws=[Raw(area=r["area"], symbol=r["symbol"], header=r.get("header", ""),
                  source=Source(**r.get("source", {}))) for r in raw.get("raws", [])],
    )
