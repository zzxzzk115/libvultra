"""Shared ABI argument transport used by the annotation frontend and its emitters."""

import hashlib
import json


def parameter_info(argument, pods=()):
    """One transport model for C tables and language declarations; pointers never imply ownership."""
    name, kind = argument["name"], argument["type"]
    info = {"name": name, "cpp_type": kind, "direction": "in", "lifetime": "value"}
    if kind == "std::string_view":
        info.update(kind="utf8", lifetime="call", fields=[("const char*", name), ("uint64_t", name + "Size")])
    elif kind.startswith("std::span<"):
        element = kind[len("std::span<"):-1]
        pointers = {"const std::byte": "const void*", "std::byte": "void*",
                    "const uint64_t": "const uint64_t*", "float": "float*"}
        if element not in pointers:
            raise RuntimeError(f"Unsupported borrowed span: {kind}")
        info.update(kind="span", element=element, lifetime="call",
                    direction="in" if element.startswith("const ") else "out",
                    fields=[(pointers[element], name), ("uint64_t", name + "Count")])
    elif kind.endswith(" &"):
        element = kind[:-2]
        if element not in {"uint8_t", "int32_t", "double"}:
            raise RuntimeError(f"Unsupported mutable ABI reference: {kind}")
        info.update(kind="reference", direction="inout", lifetime="call", fields=[(element + "*", name)])
    elif kind in {"uint8_t", "int32_t", "uint32_t", "uint64_t", "float", "double", "ObjectId"} | set(pods):
        abi_type = kind
        if kind in pods:
            abi_type = "Vultra" + kind
        elif kind == "ObjectId":
            abi_type = "uint64_t"
        info.update(kind="value", fields=[(abi_type, name)])
    else:
        raise RuntimeError(f"Unsupported ABI argument: {kind}")
    return info


def lower_argument(argument, pods=()):
    info = parameter_info(argument, pods)
    name = info["name"]
    checks = []
    expression = name
    if info["kind"] == "utf8":
        checks = [f"(!{name} && {name}Size)", f"{name}Size > SIZE_MAX"]
        expression = f'{{{name} ? {name} : "", size_t({name}Size)}}'
    elif info["kind"] == "span":
        element = info["element"]
        scalar = element.removeprefix("const ")
        limit = "SIZE_MAX" if scalar == "std::byte" else f"SIZE_MAX / sizeof({scalar})"
        checks = [f"(!{name} && {name}Count)", f"{name}Count > {limit}"]
        expression = f'{{static_cast<{element}*>({name}), size_t({name}Count)}}'
    elif info["kind"] == "reference":
        checks = [f"!{name}"]
        expression = "*" + name
    return info["fields"], expression, checks


def result_fields(kind, pods=(), *, boolean_name="value"):
    if kind == "void":
        return []
    if kind == "bool":
        return [("uint8_t*", boolean_name)]
    if kind == "std::string_view":
        return [("const char**", "data"), ("uint64_t*", "size")]
    if kind == "std::string":
        return [("char*", "data"), ("uint64_t", "capacity"), ("uint64_t*", "size")]
    info = parameter_info({"name": "value", "type": kind}, pods)
    return [(info["fields"][0][0] + "*", "value")]


def csharp_type(kind):
    scalar = {"uint8_t": "byte", "int32_t": "int", "uint32_t": "uint", "uint64_t": "ulong",
              "float": "float", "double": "double", "char": "byte", "void": "void"}
    kind = kind.removeprefix("const ")
    pointer = "*" * (len(kind) - len(kind.rstrip("*")))
    value = kind.rstrip("*")
    if value not in scalar and not value.startswith("Vultra"):
        raise RuntimeError(f"Unsupported managed ABI type: {kind}")
    return scalar.get(value, value) + pointer


def fingerprint(ir, domain):
    """Identify a table's complete declared contract, independently of source paths and unrelated domains."""
    functions = ir.get(domain + "_functions", [])
    used = {argument["type"] for function in functions for argument in function["arguments"]}
    used.update(function["return"] for function in functions)
    pods = {pod["name"]: pod for pod in ir.get("pods", []) + ir.get("experiment_pods", []) if pod["name"] in used}
    entries = []
    for function in functions:
        entries.append({"name": function["name"], "return": function["return"],
                        "parameters": [parameter_info(argument, pods) for argument in function["arguments"]],
                        "outputs": result_fields(function["return"], pods),
                        "constraints": {key: function[key] for key in
                                        ("const", "scene_mutable", "host_mutable", "project_required", "project_optional")
                                        if key in function}})
    contract = {"version": 1, "calling_convention": "cdecl", "domain": domain, "functions": entries, "pods": pods}
    encoded = json.dumps(contract, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return int.from_bytes(hashlib.sha256(encoded).digest()[:8], "big")
