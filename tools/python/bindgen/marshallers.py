"""Per-token marshalling realizations.

The IR names a type by a neutral token (bool/vec3/uuid/enum/...). Each backend
owns how that token becomes concrete: the Lua/sol2 backend maps `vec3` to
`ScriptVec3` with `toScript`/`fromScript`, a future C# backend would map it to
`System.Numerics.Vector3`. Keeping the table here (not inline in a backend)
makes adding a language a matter of adding a column.
"""

from __future__ import annotations

# token -> LuaLS (---@param / ---@field / ---@return) type name
LUALS = {
    "void": "nil",
    "bool": "boolean",
    "int": "integer",
    "uint": "integer",
    "uint64": "integer",
    "float": "number",
    "double": "number",
    "string": "string",
    "vec2": "Vec2",
    "vec3": "Vec3",
    "vec4": "Vec4",
    "uuid": "string",
    "entity": "Entity",
    "enum": "integer",
}

# token -> the C++ type a sol2 lambda accepts/returns on the Lua boundary.
# enum/struct resolve to their `cpp` field; enums cross the boundary as int.
LUA_CPP = {
    "bool": "bool",
    "int": "int",
    "uint": "std::uint32_t",
    "uint64": "std::uint64_t",
    "float": "float",
    "double": "double",
    "string": "std::string",
    "vec2": "ScriptVec2",
    "vec3": "ScriptVec3",
    "vec4": "ScriptVec4",
    "uuid": "std::string",
    "entity": "ScriptEntity",
    "enum": "int",
}

# default null-service return expression, keyed by token
LUA_NULL_RETURN = {
    "bool": "false",
    "int": "0",
    "uint": "0u",
    "uint64": "0ull",
    "float": "0.0f",
    "double": "0.0",
    "string": "std::string {}",
    "vec2": "ScriptVec2 {}",
    "vec3": "ScriptVec3 {}",
    "vec4": "ScriptVec4 {}",
    "entity": "ScriptEntity {}",
    "enum": "0",
}

_VEC_TOKENS = {"vec2", "vec3", "vec4"}


def luals_type(ref_type: str, cpp: str | None) -> str:
    if ref_type == "enum" and cpp:
        # name the enum table so LuaLS links it (Lua value is still an int)
        return cpp.split("::")[-1]
    if ref_type == "struct" and cpp:
        return cpp[len("Script"):] if cpp.startswith("Script") else cpp
    return LUALS.get(ref_type, "any")


def lua_boundary_cpp(ref_type: str, cpp: str | None) -> str:
    if ref_type == "raw" and cpp:
        return cpp  # verbatim C++ spelling (already includes const&/etc.)
    if ref_type in ("enum",):
        return "int"
    if ref_type == "struct" and cpp:
        return cpp
    return LUA_CPP.get(ref_type, "sol::object")


def default_null_return(ref_type: str, cpp: str | None) -> str:
    if ref_type == "struct" and cpp:
        return f"{cpp} {{}}"
    return LUA_NULL_RETURN.get(ref_type, "{}")


def is_vec(ref_type: str) -> bool:
    return ref_type in _VEC_TOKENS
