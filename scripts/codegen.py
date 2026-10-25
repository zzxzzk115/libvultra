#!/usr/bin/env python3
"""Generate the small public ABI and Inspector surface from annotated C++ declarations."""

import argparse
import fnmatch
import json
import math
import re
import subprocess
import sys
from pathlib import Path

from clang import cindex
import codegen_compute
from codegen_types import parameter_info, lower_argument, result_fields, csharp_type, fingerprint

ROOT = Path(__file__).resolve().parents[1]
DATABASE = ROOT / "compile_commands.json"
OUTPUT = ROOT / "source/api"
ABI_VERSION = 1


def source_path(file):
    return str(file).replace("\\", "/")


def translation_unit(database, source, *, skip_bodies=True, declaration=None):
    row = next((row for row in database if source_path(row["file"]).endswith(source)), None)
    if row is None:
        raise RuntimeError(
            f"compile_commands.json has no entry for {source}; run xmake first"
        )
    args = list(row["arguments"][1:])
    directory = Path(row.get("directory", ROOT)).resolve()
    args.insert(0, "-working-directory=" + directory.as_posix())
    compiler = source_path(row["arguments"][0]).rsplit("/", 1)[-1].lower()
    msvc = compiler in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")
    if msvc:
        # xmake supplies MSVC's system headers; select the CRT's Clang-compatible offsetof.
        args[:0] = ["--driver-mode=cl", "-D_CRT_USE_BUILTIN_OFFSETOF"]
    for option in ("-o",):
        if option in args:
            index = args.index(option)
            del args[index : index + 2]
    args = [arg for arg in args if arg not in ("-c", row["file"])]
    if not msvc:
        resource_dir = subprocess.check_output(
            ["clang", "-print-resource-dir"], text=True
        ).strip()
        args.append(f"-resource-dir={resource_dir}")
    entry = Path(source_path(declaration if declaration is not None else row["file"]))
    if not entry.is_absolute():
        entry = directory / entry
    unit = cindex.Index.create().parse(
        entry.as_posix(),
        args=args,
        options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES if skip_bodies else 0,
    )
    failures = [
        str(diagnostic)
        for diagnostic in unit.diagnostics
        if diagnostic.severity >= cindex.Diagnostic.Error
    ]
    if failures:
        raise RuntimeError("libclang failed:\n" + "\n".join(failures[:10]))
    return unit


def walk(cursor):
    for child in cursor.get_children():
        yield child
        if not child.location.file:
            continue
        file = Path(source_path(child.location.file.name)).resolve()
        if "/include/vultra/" in file.as_posix() or (
                file.is_relative_to(ROOT) and not child.location.is_in_system_header):
            yield from walk(child)


def annotations(cursor):
    return [
        child.spelling
        for child in cursor.get_children()
        if child.kind == cindex.CursorKind.ANNOTATE_ATTR
    ]


def public_header(cursor):
    file = source_path(cursor.location.file.name)
    parts = Path(file).parts
    if "include" not in parts:
        raise RuntimeError(f"Annotated API requires a public include header: {file}:{cursor.location.line}")
    return "/".join(parts[parts.index("include") + 1:])


def declaration_info(cursor):
    """Bindings follow declarations, so moving a public header needs no emitter change."""
    names = [cursor.spelling]
    parent = cursor.semantic_parent
    while parent.kind != cindex.CursorKind.TRANSLATION_UNIT:
        if parent.kind != cindex.CursorKind.NAMESPACE or not parent.spelling:
            raise RuntimeError(f"Bound free function requires a named namespace: {cursor.spelling}")
        names.append(parent.spelling)
        parent = parent.semantic_parent
    return {"header": public_header(cursor), "qualified_name": "::".join(reversed(names))}


def bound_declarations(units, marker, kind):
    declarations = {}
    names = {}
    for unit in units:
        for cursor in walk(unit.cursor):
            if marker not in annotations(cursor):
                continue
            if cursor.kind != kind:
                raise RuntimeError(f"{marker} has unsupported declaration kind: {cursor.spelling}")
            identity = cursor.get_usr()
            if identity in declarations:
                continue
            if any(not argument.spelling for argument in cursor.get_arguments()):
                raise RuntimeError(f"Bound API needs named arguments: {cursor.spelling}")
            # An overload cannot share one C function-table member with another signature.
            if cursor.spelling in names:
                raise RuntimeError(f"Ambiguous {marker} export: {cursor.spelling}")
            names[cursor.spelling] = identity
            declarations[identity] = cursor
    return sorted(declarations.values(), key=lambda cursor: cursor.spelling)


def validate_annotations(units):
    declarations = {"vultra.reflect", "vultra.reflect.node", "vultra.reflect.resource",
                    "vultra.reflect.component", "vultra.bind.pod", "vultra.bind.enum", "vultra.abi_table"}
    functions = {"vultra.bind.ui", "vultra.bind.scene", "vultra.bind.experiment", "vultra.bind.compute"}
    for unit in units:
        for cursor in walk(unit.cursor):
            markers = set(annotations(cursor))
            for marker in markers:
                if (marker.startswith("vultra.") and marker not in declarations | functions
                        and not marker.startswith(("vultra.property:", "vultra.reference:"))):
                    raise RuntimeError(f"Unknown API annotation {marker}: {cursor.spelling}")
            bindings = markers & functions
            if len(bindings) > 1:
                raise RuntimeError(f"Conflicting binding domains: {cursor.spelling}")
            if bindings:
                expected = cindex.CursorKind.CXX_METHOD if bindings & {"vultra.bind.experiment", "vultra.bind.compute"} else cindex.CursorKind.FUNCTION_DECL
                if cursor.kind != expected:
                    raise RuntimeError(f"Unsupported binding declaration: {cursor.spelling}: {cursor.kind}")
                if cursor.type.is_function_variadic():
                    raise RuntimeError(f"Bound API must be non-variadic: {cursor.spelling}")
            type_markers = markers & declarations
            reference_markers = {marker for marker in markers if marker.startswith("vultra.reference:")}
            if reference_markers and (len(reference_markers) != 1 or cursor.kind not in (
                    cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.CLASS_DECL)):
                raise RuntimeError(f"Persistent reference requires one record category: {cursor.spelling}")
            if len(type_markers) > 1 and type_markers != {"vultra.reflect", "vultra.bind.pod"}:
                raise RuntimeError(f"Conflicting type annotations: {cursor.spelling}")
            for marker in type_markers:
                expected = (cindex.CursorKind.ENUM_DECL,) if marker == "vultra.bind.enum" else (
                    cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.CLASS_DECL)
                if cursor.kind not in expected:
                    raise RuntimeError(f"{marker} has unsupported declaration kind: {cursor.spelling}")


def declaration_includes(items):
    return "".join(f"#include <{header}>\n" for header in sorted({item["header"] for item in items}))


def merge_declarations(groups, description):
    declarations = {}
    for group in groups:
        for item in group:
            name = item["name"]
            if name in declarations and declarations[name] != item:
                raise RuntimeError(f"Conflicting {description}: {name}")
            declarations[name] = item
    return sorted(declarations.values(), key=lambda item: item["name"])


def parse_api(database, sources, callback_headers=()):
    units = {name: translation_unit(database, source) for name, source in sources.items()}
    validate_annotations(units.values())
    # Source labels identify compile commands, not binding domains. Attributes own discovery.
    domain_units = {}
    for domain in ("ui", "scene", "experiment", "compute"):
        marker = "vultra.bind." + domain
        domain_units[domain] = [unit for unit in units.values()
                                if any(marker in annotations(cursor) for cursor in walk(unit.cursor))]
    # Read declaration roots with the host's compile flags, without depending on its generated-layout assertions.
    callbacks = []
    for header in callback_headers:
        callbacks.extend(parse_callback_tables(translation_unit(
            database, sources["callbacks"], declaration=ROOT / header)))
    callback_types = {}
    for table in callbacks:
        if table["name"] in callback_types and table != callback_types[table["name"]]:
            raise RuntimeError(f"Conflicting callback table declarations: {table['name']}")
        callback_types[table["name"]] = table
    callbacks = sorted(callback_types.values(), key=lambda item: item["name"])
    functions = []
    for cursor in bound_declarations(domain_units["ui"], "vultra.bind.ui", cindex.CursorKind.FUNCTION_DECL):
        signature = [argument.type.spelling for argument in cursor.get_arguments()]
        ui_types = {"std::string_view", "uint8_t &", "int32_t &", "double &", "int32_t", "double"}
        if (not signature or signature[0] != "EditorGuiFrame &" or
            any(kind not in ui_types for kind in signature[1:]) or
            cursor.result_type.spelling not in ("bool", "void")):
            raise RuntimeError(
                f"Unsupported UI ABI signature: {cursor.spelling}({signature})"
            )
        functions.append(
            {
                "name": cursor.spelling,
                "return": cursor.result_type.spelling,
                "arguments": [{"name": argument.spelling, "type": argument.type.spelling}
                              for argument in list(cursor.get_arguments())[1:]],
                **declaration_info(cursor),
            }
        )
    functions.sort(key=lambda item: item["name"])
    export_names = [ui_name(function) for function in functions]
    if len(set(export_names)) != len(export_names) or set(export_names) & {"version", "struct_size", "fingerprint"}:
        raise RuntimeError("Ambiguous UI C export names")
    for function in functions:
        ui_arguments(function)
    pods = merge_declarations([parse_pods(unit) for unit in domain_units["scene"]], "ABI POD")
    enums = merge_declarations([parse_enums(unit) for unit in domain_units["scene"]], "ABI enum")
    pod_names = {pod["name"] for pod in pods}
    scene_functions = []
    for cursor in bound_declarations(domain_units["scene"], "vultra.bind.scene", cindex.CursorKind.FUNCTION_DECL):
        arguments = [
            {"name": argument.spelling, "type": argument.type.spelling}
            for argument in cursor.get_arguments()
        ]
        result = cursor.result_type.spelling
        project_required = len(arguments) > 1 and arguments[1]["type"] == "const ProjectManifest &"
        project_optional = len(arguments) > 1 and arguments[1]["type"] == "const ProjectManifest *"
        call_arguments = arguments[2:] if project_required or project_optional else arguments[1:]
        supported_arguments = {"ObjectId", "uint64_t", "int32_t", "double", "std::string_view"} | pod_names
        if (
            not cursor.spelling.startswith("scene")
            or not arguments
            or arguments[0]["type"] not in ("const SceneTree &", "SceneTree &")
            or any(arg["type"] not in supported_arguments for arg in call_arguments)
            or result not in {"ObjectId", "uint64_t", "int32_t", "double", "std::string_view", "std::string", "void"} | pod_names
        ):
            raise RuntimeError(
                f"Unsupported scene ABI signature: {cursor.spelling}({arguments}) -> {result}"
            )
        scene_functions.append(
            {
                "name": cursor.spelling,
                "return": result,
                "scene_mutable": arguments[0]["type"] == "SceneTree &",
                "project_required": project_required,
                "project_optional": project_optional,
                "arguments": call_arguments,
                **declaration_info(cursor),
            }
        )
    scene_functions.sort(key=lambda item: item["name"])
    exports = [c_name(function["name"]) for function in scene_functions]
    if len(set(exports)) != len(exports) or set(exports) & {"version", "struct_size", "fingerprint"}:
        raise RuntimeError("Ambiguous scene C export names")

    reflected = {}
    for unit in units.values():
        for item in parse_reflection(unit):
            name = item["name"]
            if name in reflected and reflected[name] != item:
                raise RuntimeError(f"Conflicting reflected declarations: {name}")
            reflected[name] = item
    structs = sorted(reflected.values(), key=lambda item: item["name"])
    experiments = [parse_experiments(unit, pods) for unit in domain_units["experiment"]]
    experiment_pods = merge_declarations([items[0] for items in experiments], "experiment POD")
    experiment_functions = merge_declarations([items[1] for items in experiments], "experiment method")
    ir = {"format": "vultra.api", "version": 1,
            "ui_functions": functions, "scene_functions": scene_functions, "pods": pods, "types": structs,
            "experiment_pods": experiment_pods, "experiment_functions": experiment_functions, "callback_tables": callbacks, "enums": enums}
    if domain_units["compute"]:
        ir["compute_functions"] = codegen_compute.parse(domain_units["compute"], sys.modules[__name__])
    for domain in ("ui", "scene", "experiment", "compute"):
        records = {pod["name"] for pod in pods + experiment_pods}
        for function in ir.get(domain + "_functions", []):
            function["signature"] = {
                "domain": domain,
                "receiver_lifetime": {"compute": "context", "experiment": "session"}.get(domain, "callback"),
                "parameters": [parameter_info(argument, records) for argument in function["arguments"]],
                "outputs": result_fields(function["return"], records, boolean_name="changed" if domain == "ui" else "value"),
                "result_transport": "copied_utf8" if function["return"] == "std::string" else
                                    "borrowed_utf8" if function["return"] == "std::string_view" else "value",
            }
    ir["fingerprints"] = {domain: f"{fingerprint(ir, domain):016x}"
                          for domain in ("ui", "scene", "experiment", "compute")
                          if ir.get(domain + "_functions")}
    return ir


def parse_pods(unit):
    pods = []
    for cursor in walk(unit.cursor):
        if cursor.kind != cindex.CursorKind.STRUCT_DECL or "vultra.bind.pod" not in annotations(cursor):
            continue
        fields = []
        offset = 0
        alignment = 1
        for field in cursor.get_children():
            if field.kind != cindex.CursorKind.FIELD_DECL:
                continue
            if field.type.spelling not in ("float", "uint32_t", "uint64_t") or field.is_bitfield():
                raise RuntimeError(f"Unsupported ABI POD field: {cursor.spelling}.{field.spelling}: {field.type.spelling}")
            size = field.type.get_size()
            field_alignment = field.type.get_align()
            offset = (offset + field_alignment - 1) // field_alignment * field_alignment
            if field.get_field_offsetof() != offset * 8 or field.access_specifier != cindex.AccessSpecifier.PUBLIC:
                raise RuntimeError(f"Unsupported ABI POD field layout: {cursor.spelling}.{field.spelling}")
            fields.append({"name": field.spelling, "type": field.type.spelling, "offset": offset})
            offset += size
            alignment = max(alignment, field_alignment)
        if (not fields or cursor.type.get_size() != (offset + alignment - 1) // alignment * alignment
                or cursor.type.get_align() != alignment):
            raise RuntimeError(f"Unsupported ABI POD layout: {cursor.spelling}")
        pods.append({"name": cursor.spelling, "fields": fields, "size": cursor.type.get_size(), "alignment": alignment})
    return sorted(pods, key=lambda item: item["name"])


def parse_enums(unit):
    enums = []
    for cursor in walk(unit.cursor):
        if cursor.kind != cindex.CursorKind.ENUM_DECL or "vultra.bind.enum" not in annotations(cursor):
            continue
        kind = cursor.enum_type.spelling
        if kind not in ("uint8_t", "uint16_t", "uint32_t"):
            raise RuntimeError(f"Unsupported ABI enum: {cursor.spelling}: {kind}")
        values = []
        for value in cursor.get_children():
            if value.kind == cindex.CursorKind.ENUM_CONSTANT_DECL:
                if not re.fullmatch(r"e[A-Z][A-Za-z0-9]*", value.spelling) or not 0 <= value.enum_value <= 2147483647:
                    raise RuntimeError(f"Unsupported ABI enum value: {cursor.spelling}.{value.spelling}")
                values.append({"name": value.spelling[1:], "value": value.enum_value})
        enums.append({"name": cursor.spelling, "type": kind, "values": values})
    return sorted(enums, key=lambda item: item["name"])


def abi_field_name(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def parse_callback_tables(unit):
    tables = []
    allowed = {"void*", "void**", "char*", "const char*", "uint32_t", "uint64_t", "uint64_t*", "double", "VultraSceneFrame"}
    normalize = lambda value: value.replace(" *", "*")
    declarations = [cursor for cursor in walk(unit.cursor)
                    if cursor.kind == cindex.CursorKind.STRUCT_DECL and "vultra.abi_table" in annotations(cursor)]
    table_pointers = {f"const {cursor.spelling}*": f"const Vultra{cursor.spelling}*" for cursor in declarations}
    for cursor in declarations:
        fields = []
        offset = 0
        alignment = 1
        for field in cursor.get_children():
            if field.kind != cindex.CursorKind.FIELD_DECL:
                continue
            if field.access_specifier != cindex.AccessSpecifier.PUBLIC or field.is_bitfield():
                raise RuntimeError(f"Unsupported ABI table field: {cursor.spelling}.{field.spelling}")
            item = {"name": field.spelling}
            if field.type.kind == cindex.TypeKind.POINTER and field.type.get_pointee().kind == cindex.TypeKind.FUNCTIONPROTO:
                function = field.type.get_pointee()
                arguments = [normalize(value.spelling) for value in function.argument_types()]
                if (function.is_function_variadic() or function.get_result().spelling != "VultraStatus"
                        or any(value not in allowed and value not in table_pointers for value in arguments)):
                    raise RuntimeError(f"Unsupported ABI callback: {cursor.spelling}.{field.spelling}")
                arguments = [table_pointers.get(value, value) for value in arguments]
                item.update({"return": "VultraStatus", "arguments": arguments})
            elif normalize(field.type.spelling) in ("uint32_t", "void*"):
                item["type"] = normalize(field.type.spelling)
            else:
                raise RuntimeError(f"Unsupported ABI table field: {cursor.spelling}.{field.spelling}")
            size = field.type.get_size()
            field_alignment = field.type.get_align()
            offset = (offset + field_alignment - 1) // field_alignment * field_alignment
            if field.get_field_offsetof() != offset * 8:
                raise RuntimeError(f"Unsupported ABI table layout: {cursor.spelling}")
            offset += size
            alignment = max(alignment, field_alignment)
            fields.append(item)
        if fields[:2] != [{"name": "version", "type": "uint32_t"}, {"name": "structSize", "type": "uint32_t"}]:
            raise RuntimeError(f"ABI table requires version/structSize: {cursor.spelling}")
        if (cursor.type.get_size() != (offset + alignment - 1) // alignment * alignment
                or cursor.type.get_align() != alignment):
            raise RuntimeError(f"Unsupported ABI table layout: {cursor.spelling}")
        tables.append({"name": cursor.spelling, "fields": fields})
    return sorted(tables, key=lambda item: item["name"])


def callback_header(ir, names=None):
    result = "// Generated by scripts/codegen.py. Do not edit.\n#pragma once\n#include <vultra/api/vultra_scene.generated.h>\n\n"
    tables = [table for table in ir["callback_tables"] if names is None or table["name"] in names]
    if names is not None and names - {table["name"] for table in tables}:
        raise ValueError(f"Missing callback table declarations: {sorted(names - {table['name'] for table in tables})}")
    for table in tables:
        result += f"typedef struct Vultra{table['name']} Vultra{table['name']};\n"
    result += "\n"
    for table in tables:
        result += f"struct Vultra{table['name']}\n{{\n"
        for field in table["fields"]:
            name = abi_field_name(field["name"])
            if "type" in field:
                result += f"    {field['type']} {name};\n"
            else:
                result += f"    VultraStatus (VULTRA_ABI_CALL *{name})({', '.join(field['arguments'])});\n"
        result += "};\n\n"
    return result


def script_property_adapter(ir):
    table = next((value for value in ir["callback_tables"] if value["name"] == "ScriptPropertyApi"), None)
    signatures = {
        "describe": ["void*", "char*", "uint64_t", "uint64_t*"],
        "read": ["void*", "VultraSceneFrame", "const char*", "uint64_t", "char*", "uint64_t", "uint64_t*"],
        "write": ["void*", "VultraSceneFrame", "const char*", "uint64_t", "const char*", "uint64_t"],
        "prepare": ["void*", "VultraSceneFrame"],
    }
    if (table is None or len(table["fields"]) != 6 or
            any(field.get("arguments") != signatures.get(field["name"]) for field in table["fields"][2:])):
        raise RuntimeError("Script property adapter requires the implemented describe/read/write/prepare signatures")
    return """// Generated by scripts/codegen.py. Do not edit.
#pragma once
#include <vultra/api/vultra_script_property.generated.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vultra::scripting
{
    template<class Instance>
    const VultraScriptPropertyApi& scriptPropertyApi()
    {
        static const VultraScriptPropertyApi api {
            VULTRA_ABI_VERSION, sizeof(VultraScriptPropertyApi),
            [](void* state, char* data, uint64_t capacity, uint64_t* size) -> VultraStatus
            {
                if (!state || !size || (!data && capacity)) { return VULTRA_STATUS_INVALID_ARGUMENT; }
                try
                {
                    const auto result = static_cast<Instance*>(state)->describePropertiesJson();
                    *size = result.size();
                    if (!data) { return VULTRA_STATUS_OK; }
                    if (capacity < result.size()) { return VULTRA_STATUS_BUFFER_TOO_SMALL; }
                    std::copy(result.begin(), result.end(), data);
                    return VULTRA_STATUS_OK;
                }
                catch (const std::exception& error)
                {
                    std::fprintf(stderr, "Describe script properties: %s\\n", error.what());
                    return VULTRA_STATUS_ERROR;
                }
            },
            [](void* state, VultraSceneFrame scene, const char* name, uint64_t nameSize, char* data, uint64_t capacity, uint64_t* size) -> VultraStatus
            {
                if (!state || !size || (!name && nameSize) || (!data && capacity)) { return VULTRA_STATUS_INVALID_ARGUMENT; }
                try
                {
                    const auto result = static_cast<Instance*>(state)->readExportJson(scene, {name, size_t(nameSize)});
                    *size = result.size();
                    if (!data) { return VULTRA_STATUS_OK; }
                    if (capacity < result.size()) { return VULTRA_STATUS_BUFFER_TOO_SMALL; }
                    std::copy(result.begin(), result.end(), data);
                    return VULTRA_STATUS_OK;
                }
                catch (const std::invalid_argument& error)
                {
                    std::fprintf(stderr, "Read script export: %s\\n", error.what());
                    return VULTRA_STATUS_INVALID_ARGUMENT;
                }
                catch (const std::exception& error)
                {
                    std::fprintf(stderr, "Read script export: %s\\n", error.what());
                    return VULTRA_STATUS_ERROR;
                }
            },
            [](void* state, VultraSceneFrame scene, const char* name, uint64_t nameSize, const char* json, uint64_t jsonSize) -> VultraStatus
            {
                if (!state || (!name && nameSize) || (!json && jsonSize)) { return VULTRA_STATUS_INVALID_ARGUMENT; }
                try
                {
                    static_cast<Instance*>(state)->writeExportJson(scene, {name, size_t(nameSize)}, {json, size_t(jsonSize)});
                    return VULTRA_STATUS_OK;
                }
                catch (const std::invalid_argument& error)
                {
                    std::fprintf(stderr, "Write script export: %s\\n", error.what());
                    return VULTRA_STATUS_INVALID_ARGUMENT;
                }
                catch (const std::exception& error)
                {
                    std::fprintf(stderr, "Write script export: %s\\n", error.what());
                    return VULTRA_STATUS_ERROR;
                }
            }, nullptr};
        return api;
    }
} // namespace vultra::scripting
"""


def callback_csharp(ir):
    types = {"void*": "void*", "void**": "void**", "char*": "byte*", "const char*": "byte*", "uint32_t": "uint",
             "uint64_t": "ulong", "uint64_t*": "ulong*", "double": "double", "VultraSceneFrame": "VultraSceneFrame"}
    types.update({f"const Vultra{table['name']}*": f"Vultra{table['name']}*" for table in ir["callback_tables"]})
    result = ""
    for table in ir["callback_tables"]:
        result += f"\n[StructLayout(LayoutKind.Sequential)]\ninternal unsafe struct Vultra{table['name']}\n{{\n"
        for field in table["fields"]:
            name = field["name"][0].upper() + field["name"][1:]
            if "type" in field:
                result += f"    public {types[field['type']]} {name};\n"
            else:
                arguments = [types[value] for value in field["arguments"]] + ["VultraStatus"]
                result += f"    public delegate* unmanaged[Cdecl]<{', '.join(arguments)}> {name};\n"
        result += "}\n"
    return result


def parse_experiments(unit, scene_pods=()):
    scalar_types = {"float", "double", "uint32_t", "uint64_t"}
    pods = []
    for cursor in walk(unit.cursor):
        if cursor.kind == cindex.CursorKind.STRUCT_DECL and "vultra.bind.pod" in annotations(cursor):
            fields = []
            offset = 0
            alignment = 1
            for field in cursor.get_children():
                if field.kind != cindex.CursorKind.FIELD_DECL:
                    continue
                if field.type.spelling not in scalar_types or field.is_bitfield() or field.access_specifier != cindex.AccessSpecifier.PUBLIC:
                    raise RuntimeError(f"Unsupported experiment POD field: {cursor.spelling}.{field.spelling}")
                field_alignment = field.type.get_align()
                offset = (offset + field_alignment - 1) // field_alignment * field_alignment
                if field.get_field_offsetof() != offset * 8:
                    raise RuntimeError(f"Unsupported experiment POD packing: {cursor.spelling}")
                offset += field.type.get_size()
                alignment = max(alignment, field_alignment)
                fields.append({"name": field.spelling, "type": field.type.spelling})
            expected_size = (offset + alignment - 1) // alignment * alignment
            if not fields or cursor.type.get_size() != expected_size or cursor.type.get_align() != alignment:
                raise RuntimeError(f"Unsupported experiment POD layout: {cursor.spelling}")
            pods.append({"name": cursor.spelling, "fields": fields})
    methods = bound_declarations([unit], "vultra.bind.experiment", cindex.CursorKind.CXX_METHOD)
    for method in methods:
        if method.semantic_parent.spelling != "ExperimentHost" or method.is_static_method():
            raise RuntimeError("Experiment ABI requires instance methods on ExperimentHost")
    used_types = {method.result_type.spelling for method in methods}
    used_types.update(argument.type.spelling for method in methods for argument in method.get_arguments())
    for pod in scene_pods:
        if pod["name"] in used_types and not any(existing["name"] == pod["name"] for existing in pods):
            pods.append(pod)
    pod_names = {pod["name"] for pod in pods}
    functions = []
    for method in methods:
        arguments = [{"name": argument.spelling, "type": argument.type.spelling}
                     for argument in method.get_arguments()]
        result = method.result_type.spelling
        if result not in {"void", "uint64_t", "std::string_view"} | pod_names or any(
            argument["type"] not in scalar_types | pod_names | {"std::string_view", "std::span<float>"}
            for argument in arguments
        ):
            raise RuntimeError(f"Unsupported experiment ABI signature: {method.spelling}({arguments}) -> {result}")
        functions.append({"name": method.spelling, "arguments": arguments, "return": result,
                          "host_mutable": not method.is_const_method(), "header": public_header(method)})
    if not pods or not functions:
        raise RuntimeError("Missing annotated experiment API")
    return sorted(pods, key=lambda item: item["name"]), sorted(functions, key=lambda item: item["name"])


PROPERTY_KINDS = {
    "bool": "eBool", "float": "eFloat", "double": "eDouble", "std::string": "eString",
    "int": "eInt", "int32_t": "eInt", "int64_t": "eInt", "uint32_t": "eUInt", "uint64_t": "eUInt",
    "PropertyReference": "eReference",
    "glm::vec2": "eVector2", "glm::vec3": "eVector3", "glm::vec4": "eVector4", "glm::mat4": "eMatrix4",
}
PROPERTY_FLAGS = {"serialize": 1, "inspect": 2, "bind": 4, "reload": 8}


def parse_property_metadata(cursor, field, metadata):
    properties = {}
    for entry in metadata.split(";"):
        if "=" not in entry:
            raise RuntimeError(f"Invalid property metadata: {cursor.spelling}.{field.spelling}")
        key, value = entry.split("=", 1)
        if key not in {"label", "min", "max", "widget", "speed", "options", "flags", "json", "api",
                       "set", "default", "reference", "nullable", "flatten"} or key in properties:
            raise RuntimeError(f"Unknown/duplicate property metadata: {cursor.spelling}.{field.spelling}: {key}")
        properties[key] = value
    return properties


def validate_property_range(cursor, field, properties, kind, flags, scalar_type=None):
    widget = properties.get("widget", "slider")
    if widget not in ("slider", "drag"):
        raise RuntimeError(f"Unsupported widget: {cursor.spelling}.{field.spelling}")
    numeric = kind in ("eInt", "eUInt", "eFloat", "eDouble", "eVector2", "eVector3", "eVector4")
    if numeric and "inspect" in flags and not {"min", "max"} <= properties.keys():
        raise RuntimeError(f"Numeric property {field.spelling} needs min and max")
    if widget == "drag" and (not numeric or "speed" not in properties):
        raise RuntimeError(f"Drag field {field.spelling} needs a numeric value and speed")
    if ("min" in properties) != ("max" in properties):
        raise RuntimeError(f"Property {field.spelling} needs both min and max")
    try:
        minimum = float(properties.get("min", "0"))
        maximum = float(properties.get("max", "0"))
        speed = float(properties.get("speed", "1"))
    except ValueError as error:
        raise RuntimeError(f"Invalid property number: {cursor.spelling}.{field.spelling}") from error
    if (not all(math.isfinite(value) for value in (minimum, maximum, speed)) or speed <= 0
            or ("min" in properties and minimum >= maximum)):
        raise RuntimeError(f"Invalid property range/speed: {cursor.spelling}.{field.spelling}")
    float_limit = 3.4028234663852886e38
    if speed > float_limit or (kind in ("eFloat", "eVector2", "eVector3", "eVector4") and
                              max(abs(minimum), abs(maximum)) > float_limit):
        raise RuntimeError(f"Property range/speed exceeds float storage: {field.spelling}")
    if kind in ("eInt", "eUInt") and "inspect" in flags:
        bits = (scalar_type or (field.result_type if field.kind == cindex.CursorKind.CXX_METHOD else field.type)).get_size() * 8
        lower = -(2 ** (bits - 1)) if kind == "eInt" else 0
        upper = 2 ** (bits - (1 if kind == "eInt" else 0)) - 1
        try:
            integer_min = int(properties["min"])
            integer_max = int(properties["max"])
        except ValueError as error:
            raise RuntimeError(f"Integer property range must be integral: {field.spelling}") from error
        if integer_min < lower or integer_max > upper:
            raise RuntimeError(f"Integer property range exceeds its type: {field.spelling}")
        if int(minimum) != integer_min or int(maximum) != integer_max:
            raise RuntimeError(f"Integer property range cannot be represented exactly: {field.spelling}")
        # ImGui's scalar slider reserves half of its 64-bit storage range for its arithmetic.
        slider_lower = -(2 ** 62) if kind == "eInt" else 0
        slider_upper = 2 ** (62 if kind == "eInt" else 63) - 1
        if widget == "slider" and (integer_min < slider_lower or integer_max > slider_upper):
            raise RuntimeError(f"Integer slider range exceeds ImGui's supported range: {field.spelling}")


def parse_property_choices(field, properties, flags, enum):
    enum_values = []
    choices = []
    if enum.kind == cindex.CursorKind.ENUM_DECL:
        constants = {item.spelling: item.enum_value for item in enum.get_children()
                     if item.kind == cindex.CursorKind.ENUM_CONSTANT_DECL}
        enum_values = list(dict.fromkeys(constants.values()))
        if any(value < -(2 ** 63) or value >= 2 ** 63 for value in enum_values):
            raise RuntimeError(f"Enum {field.spelling} does not fit signed 64-bit values")
        if "inspect" in flags and "options" not in properties:
            raise RuntimeError(f"Enum {field.spelling} needs options")
        for option in properties.get("options", "").split("|"):
            if not option:
                continue
            if "e" + option not in constants:
                raise RuntimeError(f"Unknown enum option {field.spelling}: {option}")
            if any(choice["value"] == constants["e" + option] for choice in choices):
                raise RuntimeError(f"Duplicate enum option {field.spelling}: {option}")
            choices.append({"label": option, "value": constants["e" + option]})
    elif "options" in properties:
        raise RuntimeError(f"Non-enum property {field.spelling} cannot have options")
    return enum_values, choices


def parse_property_default(cursor, field):
    default = None
    if field.type.spelling == "float":
        default = "0"
        initializers = [child for child in field.get_children() if child.kind.is_expression()]
        if initializers:
            # A macro annotation can make the field's token range empty. Read its initializer cursor.
            default = "".join(token.spelling for token in initializers[-1].get_tokens())
            if not re.fullmatch(r"[-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?[fF]?", default):
                raise RuntimeError(f"Unsupported reflected float default: {cursor.spelling}.{field.spelling}")
    return default


def parse_reflection(unit):
    records = {}
    identities = {}
    for cursor in walk(unit.cursor):
        category = next((value for value in annotations(cursor)
                         if value in ("vultra.reflect", "vultra.reflect.node", "vultra.reflect.resource",
                                      "vultra.reflect.component")), None)
        if cursor.kind not in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.CLASS_DECL) or not category:
            continue
        if cursor.spelling in identities and identities[cursor.spelling] != cursor.get_usr():
            raise RuntimeError(f"Ambiguous reflected type name: {cursor.spelling}")
        identities[cursor.spelling] = cursor.get_usr()
        object_type = category in ("vultra.reflect.node", "vultra.reflect.resource")
        if not object_type and any(child.kind == cindex.CursorKind.CONSTRUCTOR for child in cursor.get_children()):
            raise RuntimeError(f"Reflected settings require field initializers, not constructors: {cursor.spelling}")
        fields = []
        bases = [child for child in cursor.get_children() if child.kind == cindex.CursorKind.CXX_BASE_SPECIFIER
                 and any(value.startswith("vultra.reflect") for value in annotations(child.type.get_declaration()))]
        if len(bases) > 1 or any(base.access_specifier != cindex.AccessSpecifier.PUBLIC for base in bases):
            raise RuntimeError(f"Reflected type needs at most one public reflected base: {cursor.spelling}")
        for field in cursor.get_children():
            if field.kind not in (cindex.CursorKind.FIELD_DECL, cindex.CursorKind.CXX_METHOD):
                continue
            metadata = next((a[len("vultra.property:"):] for a in annotations(field)
                             if a.startswith("vultra.property:")), None)
            if metadata is None:
                continue
            method = field.kind == cindex.CursorKind.CXX_METHOD
            if field.access_specifier != cindex.AccessSpecifier.PUBLIC or (not method and field.is_bitfield()):
                raise RuntimeError(f"Unsupported reflected property access: {cursor.spelling}.{field.spelling}")
            properties = parse_property_metadata(cursor, field, metadata)
            if "flatten" in properties:
                target = field.type.get_declaration()
                if (method or properties["flatten"] != "true" or set(properties) - {"flatten", "json"}
                        or "vultra.reflect" not in annotations(target)):
                    raise RuntimeError(f"Flatten requires a reflected value field and JSON prefix: {field.spelling}")
                path = properties.get("json", "/" + field.spelling)
                if not path.startswith("/") or re.search(r"~(?![01])", path):
                    raise RuntimeError(f"Invalid property JSON path: {field.spelling}")
                fields.append({"name": field.spelling, "flatten_type": target.spelling, "json_path": path})
                continue
            value_type = field.result_type if method else field.type
            if method:
                if (not field.is_const_method() or field.is_static_method() or list(field.get_arguments())
                        or not {"set", "default"} <= properties.keys()):
                    raise RuntimeError(f"Property getter requires const/no arguments, setter and default: {field.spelling}")
                setters = [child for child in cursor.get_children() if child.kind == cindex.CursorKind.CXX_METHOD
                           and child.spelling == properties["set"] and child.access_specifier == cindex.AccessSpecifier.PUBLIC
                           and not child.is_static_method() and not child.is_const_method()
                           and child.result_type.spelling == "void" and len(list(child.get_arguments())) == 1]
                if len(setters) != 1:
                    raise RuntimeError(f"Property setter must be unambiguous: {field.spelling}")
                if value_type.kind == cindex.TypeKind.LVALUEREFERENCE:
                    value_type = value_type.get_pointee()
                setter_type = list(setters[0].get_arguments())[0].type
                if setter_type.kind == cindex.TypeKind.LVALUEREFERENCE:
                    setter_type = setter_type.get_pointee()
                if re.sub(r"^const ", "", setter_type.spelling) != re.sub(r"^const ", "", value_type.spelling):
                    raise RuntimeError(f"Property getter/setter types differ: {field.spelling}")
            elif "set" in properties or "default" in properties:
                raise RuntimeError(f"Field properties use C++ initializers: {field.spelling}")
            field_type = re.sub(r"^const ", "", value_type.spelling)
            array = re.fullmatch(r"std::vector<(.+)>", field_type)
            scalar_type = array[1] if array else field_type
            scalar_clang_type = value_type.get_template_argument_type(0) if array else value_type
            enum = scalar_clang_type.get_declaration()
            is_enum = enum.kind == cindex.CursorKind.ENUM_DECL and not array
            reference_marker = next((marker.removeprefix("vultra.reference:")
                                     for marker in annotations(scalar_clang_type.get_declaration())
                                     if marker.startswith("vultra.reference:")), None)
            if reference_marker is not None:
                members = list(scalar_clang_type.get_declaration().type.get_fields())
                if (not re.fullmatch(r"[a-z][a-z0-9_]*", reference_marker) or len(members) != 1
                        or members[0].spelling != "value" or members[0].type.get_declaration().spelling != "StableId"
                        or members[0].access_specifier != cindex.AccessSpecifier.PUBLIC):
                    raise RuntimeError(f"Persistent reference requires one public StableId value: {scalar_type}")
            if scalar_type not in PROPERTY_KINDS and not is_enum and reference_marker is None:
                raise RuntimeError(f"Unsupported reflected property: {cursor.spelling}.{field.spelling}: {field_type}")
            if reference_marker is not None:
                scalar_kind = "eReference"
            elif is_enum:
                scalar_kind = "eEnum"
            else:
                scalar_kind = PROPERTY_KINDS[scalar_type]
            kind = "eArray" if array else scalar_kind
            flags = properties.get("flags", "serialize|inspect|bind").split("|")
            if any(flag not in PROPERTY_FLAGS for flag in flags) or len(set(flags)) != len(flags):
                raise RuntimeError(f"Invalid property flags: {cursor.spelling}.{field.spelling}")
            validate_property_range(cursor, field, properties, scalar_kind, flags, scalar_clang_type)
            enum_values, choices = parse_property_choices(field, properties, flags, enum)
            if scalar_kind == "eReference":
                if not properties.get("reference") or properties.get("nullable", "true") not in ("true", "false"):
                    raise RuntimeError(f"Reference property needs a target type and nullable boolean: {field.spelling}")
            elif "reference" in properties or "nullable" in properties:
                raise RuntimeError(f"Reference metadata on a scalar property: {field.spelling}")
            path = properties.get("json", "/" + field.spelling)
            if not path.startswith("/") or re.search(r"~(?![01])", path):
                raise RuntimeError(f"Invalid property JSON path: {cursor.spelling}.{field.spelling}")
            default = properties.get("default") if method else parse_property_default(cursor, field)
            fields.append({"name": field.spelling, "type": field_type, **properties, "kind": kind,
                           "reference_category": reference_marker or "object",
                           "default_cpp": default, "accessor": method,
                           "element_kind": scalar_kind, "element_type": scalar_type,
                           "flag_bits": sum(PROPERTY_FLAGS[flag] for flag in flags), "json_path": path,
                           "choices": choices, "enum_values": enum_values,
                           "integer_bits": scalar_clang_type.get_size() * 8 if scalar_kind in ("eInt", "eUInt") else 0})
        if not fields and not bases and not object_type and category != "vultra.reflect.component":
            raise RuntimeError(f"Reflected type has no annotated properties: {cursor.spelling}")
        header = public_header(cursor)
        records[cursor.spelling] = {"name": cursor.spelling, "header": header, "fields": fields,
                                    "base": bases[0].type.get_declaration().spelling if bases else None,
                                    "category": {"vultra.reflect": "eValue", "vultra.reflect.node": "eNode",
                                                 "vultra.reflect.resource": "eResource",
                                                 "vultra.reflect.component": "eComponent"}[category]}

    resolved = {}
    def resolve(name, visiting):
        if name in resolved:
            return resolved[name]
        if name in visiting or name not in records:
            raise RuntimeError(f"Unresolved or cyclic reflected base: {name}")
        item = records[name].copy()
        if item["base"]:
            base = resolve(item["base"], visiting | {name})
            if base["category"] != item["category"]:
                raise RuntimeError(f"Reflected base category differs: {name}")
            item["fields"] = base["fields"] + item["fields"]
        fields = []
        for field in item["fields"]:
            if "flatten_type" not in field:
                fields.append(field)
                continue
            nested = resolve(field["flatten_type"], visiting | {name})
            if nested["category"] != "eValue":
                raise RuntimeError(f"Flatten requires a value schema: {name}.{field['name']}")
            for member in nested["fields"]:
                value = member.copy()
                value["name"] = field["name"] + "." + member["name"]
                value["access_path"] = field["name"] + "." + member.get("access_path", member["name"])
                value["json_path"] = field["json_path"].rstrip("/") + member["json_path"]
                value["default_from_object"] = True
                fields.append(value)
        item["fields"] = fields
        paths = set()
        names = set()
        for field in item["fields"]:
            path = field["json_path"]
            if (field["name"] in names or path in paths or
                    any(path.startswith(other + "/") or other.startswith(path + "/") for other in paths)):
                raise RuntimeError(f"Duplicate/overlapping inherited property: {name}.{field['name']}")
            names.add(field["name"])
            paths.add(path)
        resolved[name] = item
        return item
    return [resolve(name, set()) for name in records]


def common_header():
    return """/* Generated by scripts/codegen.py. Do not edit. */
#ifndef VULTRA_ABI_GENERATED_H
#define VULTRA_ABI_GENERATED_H
#include <stdint.h>
#if defined(_WIN32)
#define VULTRA_ABI_CALL __cdecl
#else
#define VULTRA_ABI_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define VULTRA_ABI_VERSION __ABI_VERSION__u
typedef enum VultraStatus {
    VULTRA_STATUS_OK = 0,
    VULTRA_STATUS_INVALID_ARGUMENT = 1,
    VULTRA_STATUS_INVALID_FRAME = 2,
    VULTRA_STATUS_ERROR = 3,
    VULTRA_STATUS_BUFFER_TOO_SMALL = 4,
    VULTRA_STATUS_MAX_ENUM = 0x7fffffff
} VultraStatus;
#ifdef __cplusplus
}
#endif
#endif
""".replace("__ABI_VERSION__", str(ABI_VERSION))


def ui_arguments(function):
    parameters = ["VultraUiFrame frame"]
    calls = ["current"]
    checks = []
    for argument in function["arguments"]:
        fields, expression, validation = lower_argument(argument)
        parameters.extend(f"{kind} {name}" for kind, name in fields)
        calls.append(expression)
        checks.extend(validation)
    for kind, name in result_fields(function["return"], boolean_name="changed"):
        parameters.append(f"{kind} {name}")
        checks.append(f"!{name}")
    names = [parameter.rsplit(" ", 1)[-1].lstrip("*") for parameter in parameters]
    if len(set(names)) != len(names):
        raise RuntimeError(f"Ambiguous generated UI parameter names: {function['name']}")
    return parameters, calls, checks


def ui_name(function):
    name = function["name"]
    return (name[3:] if name.startswith("gui") else name).lower()


def c_header(ir):
    declarations = []
    for function in ir["ui_functions"]:
        name = ui_name(function)
        parameters, _, _ = ui_arguments(function)
        declarations.append(
            f"    VultraStatus (VULTRA_ABI_CALL *{name})({', '.join(parameters)});"
        )
    return (
        """/* Generated by scripts/codegen.py. Do not edit. */
#ifndef VULTRA_UI_GENERATED_H
#define VULTRA_UI_GENERATED_H
#include <vultra/api/vultra_abi.generated.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct VultraUiFrame {
    void* context; /* Borrowed; valid only during the on_gui callback. */
    uint64_t serial;
} VultraUiFrame;
#define VULTRA_UI_API_FINGERPRINT UINT64_C(__FINGERPRINT__)
typedef struct VultraUiApi {
    uint32_t version;
    uint32_t struct_size;
    uint64_t fingerprint;
"""
        + "\n".join(declarations)
        + "\n} VultraUiApi;\n#ifdef __cplusplus\n}\n#endif\n#endif\n"
    ).replace("__FINGERPRINT__", hex(fingerprint(ir, "ui")))


def c_name(name):
    name = re.sub(r"(?<!^)(?=[A-Z])", "_", name[5:]).lower().replace("3_d", "3d")
    return re.sub(r"(^|_)u_int(?=_|$)", r"\1uint", name)


def scene_abi_type(kind, pods):
    if kind in pods:
        return "Vultra" + kind
    return "uint64_t" if kind == "ObjectId" else kind


def scene_header(ir):
    pod_names = {pod["name"] for pod in ir["pods"]}
    pod_declarations = []
    for pod in ir["pods"]:
        fields = "\n".join(f"    {field['type']} {field['name']};" for field in pod["fields"])
        pod_declarations.append(f"typedef struct Vultra{pod['name']} {{\n{fields}\n}} Vultra{pod['name']};")
    enum_declarations = []
    for enum in ir.get("enums", []):
        prefix = re.sub(r"(?<!^)(?=[A-Z])", "_", enum["name"]).upper()
        constants = ",\n".join(f"    VULTRA_{prefix}_{re.sub(r'(?<!^)(?=[A-Z])', '_', value['name']).upper()} = {value['value']}" for value in enum["values"])
        enum_declarations.append(f"typedef enum Vultra{enum['name']} {{\n{constants}\n}} Vultra{enum['name']};")
    declarations = []
    for function in ir["scene_functions"]:
        parameters = ["VultraSceneFrame frame"]
        for argument in function["arguments"]:
            fields, _, _ = lower_argument(argument, pod_names)
            parameters.extend(f"{kind} {name}" for kind, name in fields)
        parameters.extend(f"{kind} {name}" for kind, name in result_fields(function["return"], pod_names))
        declarations.append(
            f"    VultraStatus (VULTRA_ABI_CALL *{c_name(function['name'])})({', '.join(parameters)});"
        )
    return (
        """/* Generated by scripts/codegen.py. Do not edit. */
#ifndef VULTRA_SCENE_GENERATED_H
#define VULTRA_SCENE_GENERATED_H
#include <vultra/api/vultra_abi.generated.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct VultraSceneFrame {
    void* context; /* Borrowed; valid only during a scene lifecycle/process/input callback. */
    uint64_t serial;
} VultraSceneFrame;
"""
        + "\n".join(enum_declarations + pod_declarations)
        + """
#define VULTRA_SCENE_API_FINGERPRINT UINT64_C(__FINGERPRINT__)
typedef struct VultraSceneApi {
    uint32_t version;
    uint32_t struct_size;
    uint64_t fingerprint;
"""
        + "\n".join(declarations)
        + "\n} VultraSceneApi;\n#ifdef __cplusplus\n}\n#endif\n#endif\n"
    ).replace("__FINGERPRINT__", hex(fingerprint(ir, "scene")))


def c_source(ir):
    wrappers = []
    table = []
    for function in ir["ui_functions"]:
        wrapper_name = "abi" + function["name"][0].upper() + function["name"][1:]
        parameters, calls, checks = ui_arguments(function)
        check = "".join(f" || {item}" for item in checks)
        call = f"{function['qualified_name']}({', '.join(calls)});"
        if function["return"] == "bool":
            call = "*changed = " + call
        wrappers.append(
            f"""static VultraStatus VULTRA_ABI_CALL {wrapper_name}({', '.join(parameters)})
{{
    if (!frame.context{check})
    {{
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }}
    auto* gui = static_cast<vultra::EditorGui*>(frame.context);
    if (!gui->isCurrentFrame(frame.serial))
    {{
        return VULTRA_STATUS_INVALID_FRAME;
    }}
    try
    {{
        auto current = gui->frame();
        {call}
        return VULTRA_STATUS_OK;
    }}
    catch (const std::invalid_argument&)
    {{
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }}
    catch (...)
    {{
        return VULTRA_STATUS_ERROR;
    }}
}}"""
        )
        table.append(wrapper_name)
    return (
        """// Generated by scripts/codegen.py. Do not edit.
#include <vultra/api/ui_bridge.hpp>
#include <vultra/ui/editor_gui.hpp>
"""
        + declaration_includes(ir["ui_functions"])
        + "\n#include <cstddef>\n#include <stdexcept>\n\n"
        + "\n\n".join(wrappers)
        + f"""

namespace vultra
{{
    VultraUiFrame makeUiFrame(EditorGui& gui)
    {{
        return {{&gui, gui.frameSerial()}};
    }}

    const VultraUiApi& uiApi()
    {{
        static const VultraUiApi api {{VULTRA_ABI_VERSION, sizeof(VultraUiApi), VULTRA_UI_API_FINGERPRINT, {', '.join(table)}}};
        return api;
    }}
}} // namespace vultra
"""
    )


def scene_source(ir):
    pods = {pod["name"]: pod["fields"] for pod in ir["pods"]}
    layouts = []
    for pod in ir["pods"]:
        for prefix in ("vultra::", "Vultra"):
            name = prefix + pod["name"]
            layouts.append(f"static_assert(sizeof({name}) == {pod['size']} && alignof({name}) == {pod['alignment']});")
            for field in pod["fields"]:
                layouts.append(f"static_assert(offsetof({name}, {field['name']}) == {field['offset']});")
    wrappers = []
    table = []
    for function in ir["scene_functions"]:
        parameters = ["VultraSceneFrame frame"]
        calls = ["*access->scene"]
        if function["project_required"]:
            calls.append("*access->project")
        elif function.get("project_optional"):
            calls.append("access->project")
        text_checks = []
        for argument in function["arguments"]:
            name = argument["name"]
            kind = argument["type"]
            if kind == "std::string_view":
                parameters.extend([f"const char* {name}", f"uint64_t {name}Size"])
                calls.append(f"std::string_view {{{name} ? {name} : \"\", static_cast<size_t>({name}Size)}}")
                text_checks.extend([f"(!{name} && {name}Size != 0)", f"{name}Size > SIZE_MAX"])
            else:
                parameters.append(f"{scene_abi_type(kind, pods)} {name}")
                if kind == "ObjectId":
                    calls.append(f"vultra::ObjectId {{{name}}}")
                elif kind in pods:
                    values = ", ".join(f"{name}.{field['name']}" for field in pods[kind])
                    calls.append(f"vultra::{kind} {{{values}}}")
                else:
                    calls.append(name)
        result = function["return"]
        if result == "std::string_view":
            parameters.extend(["const char** data", "uint64_t* size"])
            check = "!data || !size"
        elif result == "std::string":
            parameters.extend(["char* data", "uint64_t capacity", "uint64_t* size"])
            check = "!size || (!data && capacity != 0)"
        elif result in pods:
            parameters.append(f"Vultra{result}* value")
            check = "!value"
        elif result == "void":
            check = None
        else:
            parameters.append(f"{scene_abi_type(result, pods)}* value")
            check = "!value"
        expression = f"{function['qualified_name']}({', '.join(calls)})"
        if result == "std::string_view":
            assign = f"const auto result = {expression};\n        *data = result.data();\n        *size = result.size();"
        elif result == "std::string":
            assign = (f"const auto result = {expression};\n        *size = result.size();\n"
                      "        if (!data) { return VULTRA_STATUS_OK; }\n"
                      "        if (capacity < result.size()) { return VULTRA_STATUS_BUFFER_TOO_SMALL; }\n"
                      "        std::copy(result.begin(), result.end(), data);")
        elif result in pods:
            values = ", ".join(f"result.{field['name']}" for field in pods[result])
            assign = f"const auto result = {expression};\n        *value = {{{values}}};"
        elif result == "void":
            assign = f"{expression};"
        else:
            assign = f"*value = {expression}" + (".value;" if result == "ObjectId" else ";")
        condition = "!frame.context" + (f" || {check}" if check else "")
        condition += "".join(f" || {item}" for item in text_checks)
        project_check = ""
        if function["project_required"]:
            project_check = """    if (!access->project)
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
"""
        wrapper_name = "abi" + function["name"][0].upper() + function["name"][1:]
        wrappers.append(
            f"""static VultraStatus VULTRA_ABI_CALL {wrapper_name}({', '.join(parameters)})
{{
    if ({condition})
    {{
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }}
    auto* access = static_cast<vultra::SceneAccess*>(frame.context);
    if (!access->active || access->serial != frame.serial || !access->scene)
    {{
        return VULTRA_STATUS_INVALID_FRAME;
    }}
{project_check}    try
    {{
        {assign}
        return VULTRA_STATUS_OK;
    }}
    catch (const std::invalid_argument&)
    {{
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }}
    catch (...)
    {{
        return VULTRA_STATUS_ERROR;
    }}
}}"""
        )
        table.append(wrapper_name)
    return (
        """// Generated by scripts/codegen.py. Do not edit.
#include <vultra/api/scene_bridge.hpp>
"""
        + declaration_includes(ir["scene_functions"])
        + "\n#include <algorithm>\n#include <cstddef>\n#include <stdexcept>\n#include <string_view>\n\n"
        + "\n".join(layouts) + "\n\n"
        + "\n\n".join(wrappers)
        + f"""

namespace vultra
{{
    const VultraSceneApi& sceneApi()
    {{
        static const VultraSceneApi api {{VULTRA_ABI_VERSION, sizeof(VultraSceneApi), VULTRA_SCENE_API_FINGERPRINT, {', '.join(table)}}};
        return api;
    }}
}} // namespace vultra
"""
    )


def experiment_name(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def experiment_parameters(function, pod_names):
    parameters = [("context", "void*")]
    for argument in function["arguments"]:
        fields, _, _ = lower_argument(argument, pod_names)
        parameters.extend((name, kind) for kind, name in fields)
    parameters.extend((name, kind) for kind, name in result_fields(function["return"], pod_names))
    return parameters


def experiment_header(ir):
    pod_names = {pod["name"] for pod in ir["experiment_pods"]}
    structs = []
    for pod in ir["experiment_pods"]:
        if any(scene_pod['name'] == pod['name'] for scene_pod in ir['pods']):
            continue
        fields = "\n".join(f"    {field['type']} {field['name']};" for field in pod["fields"])
        structs.append(f"typedef struct Vultra{pod['name']} {{\n{fields}\n}} Vultra{pod['name']};")
    declarations = []
    for function in ir["experiment_functions"]:
        parameters = ", ".join(f"{kind} {name}" for name, kind in experiment_parameters(function, pod_names))
        declarations.append(f"    VultraStatus (VULTRA_ABI_CALL *{experiment_name(function['name'])})({parameters});")
    return ("""/* Generated by scripts/codegen.py. Do not edit. */
#ifndef VULTRA_EXPERIMENT_GENERATED_H
#define VULTRA_EXPERIMENT_GENERATED_H
#include <vultra/api/vultra_abi.generated.h>
#include <vultra/api/vultra_scene.generated.h>
#ifdef __cplusplus
extern "C" {
#endif
""" + "\n".join(structs) + """
/* Opaque context is borrowed from the host. Calls must be serialized; no C++ exceptions cross this table. */
#define VULTRA_EXPERIMENT_API_FINGERPRINT UINT64_C(__FINGERPRINT__)
typedef struct VultraExperimentApi {
    uint32_t version;
    uint32_t struct_size;
    uint64_t fingerprint;
""" + "\n".join(declarations) + "\n} VultraExperimentApi;\n#ifdef __cplusplus\n}\n#endif\n#endif\n").replace("__FINGERPRINT__", hex(fingerprint(ir, "experiment")))


def experiment_source(ir):
    pods = {pod["name"]: pod["fields"] for pod in ir["experiment_pods"]}
    assertions = []
    for name, fields in pods.items():
        assertions.extend([
            f"static_assert(sizeof(Vultra{name}) == sizeof(vultra::{name}));",
            f"static_assert(alignof(Vultra{name}) == alignof(vultra::{name}));",
        ])
        assertions.extend(f"static_assert(offsetof(Vultra{name}, {field['name']}) == "
                          f"offsetof(vultra::{name}, {field['name']}));" for field in fields)
    wrappers = []
    table = []
    for function in ir["experiment_functions"]:
        name = function["name"]
        wrapper = "abi" + name[0].upper() + name[1:]
        parameters = ", ".join(f"{kind} {name}" for name, kind in experiment_parameters(function, pods))
        checks = ["!context"]
        calls = []
        for argument in function["arguments"]:
            argument_name, kind = argument["name"], argument["type"]
            if kind == "std::string_view":
                checks.extend([f"(!{argument_name} && {argument_name}Size)", f"{argument_name}Size > SIZE_MAX"])
                calls.append(f'{{{argument_name} ? {argument_name} : "", static_cast<size_t>({argument_name}Size)}}')
            elif kind == "std::span<float>":
                checks.extend([f"(!{argument_name} && {argument_name}Count)",
                               f"{argument_name}Count > SIZE_MAX / sizeof(float)"])
                calls.append(f"{{{argument_name}, static_cast<size_t>({argument_name}Count)}}")
            elif kind in pods:
                values = ', '.join(f"{argument_name}.{field['name']}" for field in pods[kind])
                calls.append(f"vultra::{kind} {{{values}}}")
            else:
                calls.append(argument_name)
        result = function["return"]
        expression = f"host.{name}({', '.join(calls)})"
        if result == "std::string_view":
            checks.extend(["!data", "!size"])
            assign = f"const auto result = {expression};\n        *data = result.data();\n        *size = result.size();"
        elif result in pods:
            checks.append("!value")
            values = ", ".join(f"result.{field['name']}" for field in pods[result])
            assign = f"const auto result = {expression};\n        *value = {{{values}}};"
        elif result == "void":
            assign = expression + ";"
        else:
            checks.append("!value")
            assign = f"*value = {expression};"
        wrappers.append(f"""static VultraStatus VULTRA_ABI_CALL {wrapper}({parameters})
{{
    if ({' || '.join(checks)})
    {{
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }}
    auto& host = *static_cast<vultra::ExperimentHost*>(context);
    try
    {{
        {assign}
        return VULTRA_STATUS_OK;
    }}
    catch (const std::invalid_argument& error)
    {{
        host.recordError("{name}", error.what());
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }}
    catch (const std::exception& error)
    {{
        host.recordError("{name}", error.what());
        return VULTRA_STATUS_ERROR;
    }}
    catch (...)
    {{
        host.recordError("{name}", "Unknown exception");
        return VULTRA_STATUS_ERROR;
    }}
}}""")
        table.append(wrapper)
    return """// Generated by scripts/codegen.py. Do not edit.
#include <vultra/research/api/experiment_bridge.hpp>
""" + declaration_includes(ir["experiment_functions"]) + "\n#include <cstddef>\n#include <stdexcept>\n\n" + "\n".join(assertions) + "\n\n" + "\n\n".join(wrappers) + f"""

namespace vultra
{{
    const VultraExperimentApi& experimentApi()
    {{
        static const VultraExperimentApi api {{VULTRA_ABI_VERSION, sizeof(VultraExperimentApi), VULTRA_EXPERIMENT_API_FINGERPRINT, {', '.join(table)}}};
        return api;
    }}
}} // namespace vultra
"""


def python_experiment_bindings(ir):
    kinds = {"uint32_t": "ctypes.c_uint32", "uint64_t": "ctypes.c_uint64",
             "float": "ctypes.c_float", "double": "ctypes.c_double", "void*": "ctypes.c_void_p",
             "const char*": "ctypes.c_char_p", "const char**": "ctypes.POINTER(ctypes.c_void_p)"}
    structs = []
    for pod in ir["experiment_pods"]:
        name = "Vultra" + pod["name"]
        fields = "\n".join(f'        ("{field["name"]}", {kinds[field["type"]]}),' for field in pod["fields"])
        structs.append(f"class {name}(ctypes.Structure):\n    _fields_ = [\n{fields}\n    ]")
        kinds[name] = name
        kinds[name + "*"] = f"ctypes.POINTER({name})"
    for kind in ("uint64_t", "float"):
        kinds[kind + "*"] = f"ctypes.POINTER({kinds[kind]})"
    fields = []
    for function in ir["experiment_functions"]:
        parameters = experiment_parameters(function, {pod["name"] for pod in ir["experiment_pods"]})
        arguments = "\n".join(f"            {kinds[kind]}," for _, kind in parameters)
        fields.append(f'        ("{experiment_name(function["name"])}", ctypes.CFUNCTYPE(\n'
                      f'            ctypes.c_int32,\n{arguments}\n        )),')
    return """# Generated by scripts/codegen.py. Do not edit.
import ctypes

ABI_VERSION = 1

""" + "\n\n".join(structs) + "\n\nclass VultraExperimentApi(ctypes.Structure):\n    _fields_ = [\n" + \
        '        ("version", ctypes.c_uint32),\n        ("struct_size", ctypes.c_uint32),\n' + \
        "\n".join(fields) + "\n    ]\n"



def python_scene_controls(ir):
    native_pods = {pod["name"]: pod for pod in ir["pods"]}
    reflected = {item["name"]: item for item in ir["types"]}
    used = {pod["name"] for pod in ir["experiment_pods"]} & native_pods.keys()
    values = []
    for name in sorted(used):
        if name not in reflected:
            continue
        fields = reflected[name]["fields"]
        if any(field["type"] != "float" for field in fields):
            raise RuntimeError(f"Unsupported Python scene value: {name}")
        declarations = []
        for field in fields:
            default = field["default_cpp"].rstrip("fF")
            declarations.append(f"    {experiment_name(field['name'])}: float = {default}")
        arguments = ", ".join(f"value.{field['name']}" for field in fields)
        conversions = ", ".join(f"_finite_float(self.{experiment_name(field['name'])})" for field in fields)
        values.append("@dataclass(frozen=True)\n" + f"class {name}:\n" + "\n".join(declarations) +
                      f"\n\n    @classmethod\n    def _from_native(cls, value):\n        return cls({arguments})\n"
                      f"\n    def _to_native(self):\n        return Vultra{name}({conversions})")
    methods = []
    for function in ir["experiment_functions"]:
        result = function["return"]
        arguments = function["arguments"]
        if len(arguments) < 2 or arguments[0]["name"] != "session" or arguments[1]["type"] != "std::string_view":
            continue
        name = experiment_name(function["name"])
        if result in used and result in reflected:
            methods.append(f"    def {name}(self, identifier: str) -> {result}:\n"
                           "        self._check()\n        text = _scene_id(identifier)\n"
                           f"        value = Vultra{result}()\n"
                           f"        self._host._status(self._host._api.{name}(\n"
                           "            self._host._native.context, self._id, text, len(text), ctypes.byref(value)))\n"
                           f"        return {result}._from_native(value)")
        elif len(arguments) == 3 and arguments[2]["type"] in used and arguments[2]["type"] in reflected:
            kind = arguments[2]["type"]
            methods.append(f"    def {name}(self, identifier: str, value: {kind}) -> None:\n"
                           "        self._check()\n        text = _scene_id(identifier)\n"
                           f"        if type(value) is not {kind}:\n"
                           f"            raise TypeError('value must be {kind}')\n"
                           f"        self._host._status(self._host._api.{name}(\n"
                           "            self._host._native.context, self._id, text, len(text), value._to_native()))")
    return """# Generated by scripts/codegen.py. Do not edit.
import ctypes
import math
from dataclasses import dataclass
from numbers import Real

from ._bindings_generated import """ + ", ".join("Vultra" + name for name in sorted(used) if name in reflected) + """

def _scene_id(value):
    if not isinstance(value, str) or not value or '\\0' in value:
        raise ValueError("identifier must be a persistent UUID string")
    return value.encode("utf-8")

def _finite_float(value):
    if isinstance(value, bool) or not isinstance(value, Real) or not math.isfinite(float(value)):
        raise ValueError("settings require finite numeric values")
    stored = ctypes.c_float(float(value)).value
    if not math.isfinite(stored):
        raise ValueError("settings value exceeds float32")
    return stored

""" + "\n\n".join(values) + "\n\nclass ExperimentSceneControls:\n" + "\n\n".join(methods) + "\n"


def type_function(item):
    name = item["name"]
    return name[0].lower() + name[1:] + "Type"


def property_header(types):
    result = "// Generated by scripts/codegen.py. Do not edit.\n#pragma once\n"
    result += "#include <vultra/core/base/property.hpp>\n"
    result += "\n".join(f"#include <{header}>" for header in sorted({item["header"] for item in types}))
    result += "\n\nnamespace vultra\n{\n"
    for item in types:
        result += f"    const ObjectTypeInfo& {type_function(item)}();\n"
    return result + "} // namespace vultra\n"


def property_source(types, header):
    result = f"// Generated by scripts/codegen.py. Do not edit.\n#include <{header}>\n\n#include <array>\n\nnamespace vultra\n{{\n"
    storage = {"eInt": "int64_t", "eUInt": "uint64_t", "eEnum": "PropertyEnum"}
    for item in types:
        name = item["name"]
        result += f"    const ObjectTypeInfo& {type_function(item)}()\n    {{\n"
        result += f"        static const std::array<PropertyInfo, {len(item['fields'])}> properties = {{{{\n"
        for index, field in enumerate(item["fields"]):
            member, kind, field_type = field["name"], field["kind"], field["type"]
            path = field.get("access_path", member)
            access = path + "()" if field.get("accessor") else path
            get = f"static_cast<const {name}*>(object)->{access}"
            default = (field["default_cpp"] if field.get("accessor") and not field.get("default_from_object")
                       else f"{name}{{}}.{access}")
            value = f"std::get<{storage.get(kind, field_type)}>(value)"
            if kind == "eEnum":
                get, default = f"PropertyEnum{{static_cast<int64_t>({get})}}", f"PropertyEnum{{static_cast<int64_t>({default})}}"
                value = f"static_cast<{field_type}>({value}.value)"
            elif kind in ("eInt", "eUInt"):
                get, default = f"static_cast<{storage[kind]}>({get})", f"static_cast<{storage[kind]}>({default})"
                value = f"static_cast<{field_type}>({value})"
            elif kind == "eReference":
                if field_type != "PropertyReference":
                    get, default = f"PropertyReference{{({get}).value}}", f"PropertyReference{{({default}).value}}"
                value = "std::get<PropertyReference>(value)"
                if field_type != "PropertyReference":
                    value = f"{field_type}{{{value}.id}}"
            elif kind == "eArray":
                scalar = field["element_type"]
                element = "element.value" if field["element_kind"] == "eReference" and scalar != "PropertyReference" else "element"
                convert = f"PropertyReference{{{element}}}" if field["element_kind"] == "eReference" else element
                if field["element_kind"] in ("eInt", "eUInt"):
                    convert = f"static_cast<{storage[field['element_kind']]}>({element})"
                get = f"[&]() {{ PropertyArray result; for (const auto& element : {get}) {{ result.values.emplace_back({convert}); }} return result; }}()"
                default = f"[&]() {{ PropertyArray result; for (const auto& element : {default}) {{ result.values.emplace_back({convert}); }} return result; }}()"
                element_value = f"std::get<{storage.get(field['element_kind'], scalar)}>(element)"
                if field["element_kind"] == "eReference":
                    element_value = "std::get<PropertyReference>(element)"
                    if scalar != "PropertyReference":
                        element_value = f"{scalar}{{{element_value}.id}}"
                value = f"[&]() {{ {field_type} result; for (const auto& element : std::get<PropertyArray>(value).values) {{ result.push_back({element_value}); }} return result; }}()"
            prefix = path.rsplit(".", 1)[0] + "." if "." in path else ""
            assignment = (f"{prefix}{field['set']}({value})" if field.get("accessor") else f"{path} = {value}")
            drawer = member if name == "RenderSettings" else f"{name}.{member}"
            strings = ", ".join(json.dumps(text) for text in (member, field.get("label", member), drawer, field["json_path"]))
            choices = "{}, {}"
            if kind == "eEnum":
                labels = ", ".join("{" + json.dumps(choice["label"]) + ", " + str(choice["value"]) + "}"
                                   for choice in field["choices"])
                values = ", ".join(str(value) for value in field["enum_values"])
                choices = "{" + labels + "}, {" + values + "}"
            widget = "eDrag" if field.get("widget") == "drag" else "eSlider"
            flags = " | ".join("PropertyFlags::e" + name[0].upper() + name[1:]
                               for name, bit in PROPERTY_FLAGS.items() if field["flag_bits"] & bit)
            reference_category = field.get("reference_category", "object")
            target = json.dumps(field.get("reference", ""))
            result += f"        {{{{{strings}, PropertyKind::{kind}, {flags}, PropertyWidget::{widget}, {field['integer_bits']}, {field.get('min', '0')}, {field.get('max', '0')}, {field.get('speed', '1')}, {choices},\n"
            result += f"            PropertyValue{{{default}}}, PropertyKind::{field.get('element_kind', 'eBool')}, {json.dumps(reference_category)}, {target}, {field.get('nullable', 'true')}}},\n"
            result += f"            [](const void* object) -> PropertyValue {{ return {get}; }},\n"
            result += f"            [](void* object, const PropertyValue& value) {{ static_cast<{name}*>(object)->{assignment}; }} }},\n"
        category = item.get("category", "eValue")[1:].lower()
        result += f'        }}}};\n        static const ObjectTypeInfo type = {{"{name}", properties, {("&" + item["base"][0].lower() + item["base"][1:] + "Type()") if item.get("base") else "nullptr"}, "{category}"}};\n        return type;\n    }}\n\n'
    return result + "} // namespace vultra\n"


def reflection_header(types):
    headers = sorted({item["header"] for item in types})
    result = "// Generated by scripts/codegen.py. Do not edit.\n#pragma once\n"
    result += "\n".join(f"#include <{header}>" for header in headers)
    result += "\n#include <vultra/ui/editor_gui_inspector.hpp>\n\nnamespace vultra\n{\n"
    for item in types:
        name = item["name"]
        result += f"    bool draw{name}(EditorGuiInspector& inspector, {name}& settings);\n"
    return result + "} // namespace vultra\n"


def reflection_source(types, header):
    result = f"// Generated by scripts/codegen.py. Do not edit.\n#include <vultra/api/{header}>\n"
    names = {item["name"] for item in types}
    if "MaterialParameters" in names:
        result += "#include <vultra/assets/material_properties.generated.hpp>\n"
    if names & {"CameraSettings", "LightSettings", "EnvironmentSettings"}:
        result += "#include <vultra/scene/render_properties.generated.hpp>\n"
    if any(item.get("category") == "eResource" for item in types):
        result += "#include <vultra/scene/resource_properties.generated.hpp>\n"
    if "RenderSettings" in names:
        result += "#include <vultra/servers/rendering/builtin/render_properties.generated.hpp>\n"
    result += "\nnamespace vultra\n{\n"
    for item in types:
        name = item["name"]
        result += f"    bool draw{name}(EditorGuiInspector& inspector, {name}& settings)\n    {{\n        return inspector.properties({type_function(item)}(), &settings);\n    }}\n\n"
    return result + "} // namespace vultra\n"


def pascal_name(name):
    return "".join(part[0].upper() + part[1:] for part in name.split("_"))


def csharp_values(ir):
    result = "// Generated by scripts/codegen.py. Do not edit.\nusing System.Numerics;\nusing Vultra.Interop;\n\nnamespace Vultra.Scripting;\n"
    for enum in ir.get("enums", []):
        kind = {"uint8_t": "byte", "uint16_t": "ushort", "uint32_t": "uint"}[enum["type"]]
        result += f"\npublic enum {enum['name']} : {kind}\n{{\n"
        for value in enum["values"]:
            result += f"    {value['name']} = {value['value']},\n"
        result += "}\n"
    pods = {pod["name"] for pod in ir["pods"]}
    for item in ir["types"]:
        if item["name"] not in pods:
            continue
        groups = {}
        for field in item["fields"]:
            tokens = field["json_path"].split("/")[1:]
            if len(tokens) not in (1, 2) or (len(tokens) == 2 and not tokens[1].isdigit()):
                raise RuntimeError(f"Unsupported managed value path: {item['name']}.{field['name']}")
            name = field.get("api", pascal_name(tokens[0]))
            if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", name):
                raise RuntimeError(f"Unsupported managed value name: {item['name']}.{field['name']}")
            groups.setdefault(name, []).append(field)
        members = []
        for name, fields in groups.items():
            if len(fields) == 1:
                if len(fields[0]["json_path"].split("/")) != 2:
                    raise RuntimeError(f"Incomplete managed vector: {item['name']}.{name}")
                value_type = "float"
            else:
                if any(len(field["json_path"].split("/")) != 3 for field in fields):
                    raise RuntimeError(f"Duplicate managed property name: {item['name']}.{name}")
                fields.sort(key=lambda field: int(field["json_path"].rsplit("/", 1)[1]))
                if len(fields) not in (2, 3, 4) or [int(field["json_path"].rsplit("/", 1)[1]) for field in fields] != list(range(len(fields))):
                    raise RuntimeError(f"Incomplete managed vector: {item['name']}.{name}")
                value_type = "Vector" + str(len(fields))
            defaults = [field["default_cpp"].rstrip("fF") + "f" for field in fields]
            default = defaults[0] if len(fields) == 1 else "new(" + ", ".join(defaults) + ")"
            members.append({"name": name, "type": value_type, "fields": fields, "default": default})
        name = item["name"]
        result += f"\npublic readonly record struct {name}\n{{\n"
        for member in members:
            result += f"    public {member['type']} {member['name']} {{ get; init; }} = {member['default']};\n"
        result += f"\n    public {name}() {{ }}\n"
        arguments = ", ".join(member["type"] + " " + member["name"][0].lower() + member["name"][1:] for member in members)
        result += f"\n    public {name}({arguments})\n    {{\n"
        for member in members:
            result += f"        {member['name']} = {member['name'][0].lower() + member['name'][1:]};\n"
        result += "    }\n"
        result += f"\n    internal static {name} FromAbi(Vultra{name} value) => new()\n    {{\n"
        for member in members:
            values = ["value." + pascal_name(field["name"]) for field in member["fields"]]
            value = values[0] if len(values) == 1 else "new " + member["type"] + "(" + ", ".join(values) + ")"
            result += f"        {member['name']} = {value},\n"
        result += "    };\n"
        result += f"\n    internal Vultra{name} ToAbi() => new()\n    {{\n"
        for member in members:
            for index, field in enumerate(member["fields"]):
                value = member["name"] if len(member["fields"]) == 1 else member["name"] + "." + "XYZW"[index]
                result += f"        {pascal_name(field['name'])} = {value},\n"
        result += "    };\n}\n"
    return result


def csharp_bindings(ir):
    pods = {pod["name"]: pod["fields"] for pod in ir["pods"]}
    pod_declarations = []
    for name, fields in pods.items():
        types = {"float": "float", "uint32_t": "uint", "uint64_t": "ulong"}
        members = "\n".join(f"    public {types[field['type']]} {field['name'][0].upper() + field['name'][1:]};" for field in fields)
        pod_declarations.append(f"[StructLayout(LayoutKind.Sequential)]\ninternal struct Vultra{name}\n{{\n{members}\n}}")
    ui_fields = []
    for function in ir["ui_functions"]:
        name = function["name"][3:] if function["name"].startswith("gui") else pascal_name(function["name"])
        fields = [field for argument in function["arguments"] for field in parameter_info(argument)["fields"]]
        fields += result_fields(function["return"], boolean_name="changed")
        arguments = ["VultraUiFrame"] + [csharp_type(kind) for kind, _ in fields] + ["VultraStatus"]
        ui_fields.append(
            f"    public delegate* unmanaged[Cdecl]<{', '.join(arguments)}> {name};"
        )
    scene_fields = []
    for function in ir["scene_functions"]:
        name = function["name"][5:]
        fields = [field for argument in function["arguments"] for field in parameter_info(argument, pods)["fields"]]
        fields += result_fields(function["return"], pods)
        arguments = ", ".join(csharp_type(kind) for kind, _ in fields)
        if arguments:
            arguments += ", "
        output = "VultraStatus"
        scene_fields.append(
            f"    public delegate* unmanaged[Cdecl]<VultraSceneFrame, {arguments}{output}> {name};"
        )
    return """// Generated by scripts/codegen.py. Do not edit.
using System.Runtime.InteropServices;

namespace Vultra.Interop;

internal static class VultraAbi
{
    public const uint Version = __ABI_VERSION__;
    public const ulong UiFingerprint = __UI_FINGERPRINT__UL;
    public const ulong SceneFingerprint = __SCENE_FINGERPRINT__UL;
}

internal enum VultraStatus : int
{
    Ok = 0,
    InvalidArgument = 1,
    InvalidFrame = 2,
    Error = 3,
    BufferTooSmall = 4
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraUiFrame
{
    public void* Context;
    public ulong Serial;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraSceneFrame
{
    public void* Context;
    public ulong Serial;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraUiApi
{
    public uint Version;
    public uint StructSize;
    public ulong Fingerprint;
""".replace("__ABI_VERSION__", str(ABI_VERSION)).replace("__UI_FINGERPRINT__", hex(fingerprint(ir, "ui"))).replace("__SCENE_FINGERPRINT__", hex(fingerprint(ir, "scene"))) + "\n".join(ui_fields) + """
}

""" + "\n\n".join(pod_declarations) + "\n\n" + """
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraSceneApi
{
    public uint Version;
    public uint StructSize;
    public ulong Fingerprint;
""" + "\n".join(scene_fields) + """
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraScriptRegistrationApi
{
    public uint Version;
    public uint StructSize;
    public void* UserData;
    public delegate* unmanaged[Cdecl]<void*, byte*, ulong, VultraStatus> RegisterClass;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraHostApi
{
    public uint Version;
    public uint StructSize;
    public VultraUiApi* Ui;
    public VultraSceneApi* Scene;
    public VultraScriptRegistrationApi* Scripts;
    public void* Extensions;
    public ulong ExtensionCount;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraPluginApi
{
    public uint Version;
    public uint StructSize;
    public void* UserData;
    public delegate* unmanaged[Cdecl]<void*, VultraSceneFrame, float, VultraStatus> Update;
    public delegate* unmanaged[Cdecl]<void*, VultraUiFrame, VultraStatus> OnGui;
    public delegate* unmanaged[Cdecl]<void*, VultraStatus> Stop;
    public delegate* unmanaged[Cdecl]<void*, VultraSceneFrame, uint, float, VultraStatus> OnScene;
    public VultraScriptPropertyApi* Properties;
}
""" + callback_csharp(ir)



def lua_values(ir):
    """Marshal annotated PODs and enums through the same C ABI schema."""
    result = """// Generated by scripts/codegen.py. Do not edit.
#pragma once

#include <vultra/api/vultra_scene.generated.h>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

#include <limits>

namespace vultra::scripting::detail
{
    inline float luaFloatField(lua_State* state, int index, const char* name)
    {
        lua_getfield(state, index, name);
        const auto value = static_cast<float>(luaL_checknumber(state, -1));
        lua_pop(state, 1);
        return value;
    }

    inline uint64_t luaUnsignedField(lua_State* state, int index, const char* name, uint64_t maximum)
    {
        lua_getfield(state, index, name);
        const auto value = luaL_checkinteger(state, -1);
        if (value < 0 || uint64_t(value) > maximum)
        {
            luaL_error(state, "Input value out of range: %s", name);
        }
        lua_pop(state, 1);
        return uint64_t(value);
    }

"""
    result += "    inline void installLuaEnums(lua_State* state)\n    {\n"
    for enum in ir.get("enums", []):
        result += "        lua_newtable(state);\n"
        for value in enum["values"]:
            result += f'        lua_pushinteger(state, {value["value"]});\n        lua_setfield(state, -2, "{value["name"]}");\n'
        result += f'        lua_setglobal(state, "{enum["name"]}");\n'
    result += "    }\n\n"
    for pod in ir["pods"]:
        name = "Vultra" + pod["name"]
        result += f"    inline void pushLuaValue(lua_State* state, const {name}& value)\n    {{\n        lua_newtable(state);\n"
        for field in pod["fields"]:
            key = re.sub(r"(?<!^)(?=[A-Z])", "_", field["name"]).lower()
            if field["type"] == "uint64_t":
                result += f'        if (value.{field["name"]} > uint64_t(std::numeric_limits<lua_Integer>::max()))\n        {{\n            luaL_error(state, "Value exceeds Lua integer range: {key}");\n        }}\n'
            call = "lua_pushnumber" if field["type"] == "float" else "lua_pushinteger"
            result += f'        {call}(state, value.{field["name"]});\n        lua_setfield(state, -2, "{key}");\n'
        result += "    }\n\n"
        result += f"    inline {name} readLua{pod['name']}(lua_State* state, int index)\n    {{\n        luaL_checktype(state, index, LUA_TTABLE);\n        index = lua_absindex(state, index);\n        return {{\n"
        for field in pod["fields"]:
            key = re.sub(r"(?<!^)(?=[A-Z])", "_", field["name"]).lower()
            if field["type"] == "float":
                expression = f'luaFloatField(state, index, "{key}")'
            else:
                kind = field["type"]
                expression = f'{kind}(luaUnsignedField(state, index, "{key}", std::numeric_limits<{kind}>::max()))'
            result += f'            {expression},\n'
        result += "        };\n    }\n\n"
    return result + "} // namespace vultra::scripting::detail\n"

def generate_outputs(ir, specifications, root):
    """Each repository declares the generated files it owns; all use this frontend and IR."""
    emitters = {
        "csharp_bindings": csharp_bindings, "csharp_values": csharp_values, "lua_values": lua_values,
        "c_header": c_header, "c_source": c_source, "scene_header": scene_header, "scene_source": scene_source,
        "script_property_adapter": script_property_adapter, "experiment_header": experiment_header,
        "experiment_source": experiment_source, "python_experiment_bindings": python_experiment_bindings,
        "python_scene_controls": python_scene_controls,
        "compute_header": codegen_compute.header, "compute_source": codegen_compute.source,
        "python_compute_bindings": codegen_compute.python_bindings,
    }
    files = {}
    for specification in specifications:
        path = (root / specification["path"]).resolve()
        if not path.is_relative_to(root) or path in files:
            raise ValueError(f"Generated output must be unique and inside its repository: {path}")
        emitter = specification["emitter"]
        if emitter == "ir":
            contents = json.dumps(ir, indent=2, sort_keys=True) + "\n"
        elif emitter == "common_header":
            contents = common_header()
        elif emitter == "callback_header":
            contents = callback_header(ir, set(specification["tables"]))
        elif emitter in ("property_header", "property_source", "reflection_header", "reflection_source"):
            headers = specification["declarations"]
            categories = specification.get("categories")
            types = [item for item in ir["types"]
                     if any(fnmatch.fnmatchcase(item["header"], header) for header in headers)
                     and (categories is None or item["category"] in categories)]
            for header in headers:
                if not any(fnmatch.fnmatchcase(item["header"], header) for item in types):
                    raise ValueError(f"Missing reflected declarations for {path}: {header}")
            if not headers:
                raise ValueError(f"Reflection output needs declaration headers: {path}")
            arguments = [types]
            if emitter.endswith("_source"):
                arguments.append(specification["header"])
            contents = {"property_header": property_header, "property_source": property_source,
                        "reflection_header": reflection_header, "reflection_source": reflection_source}[emitter](*arguments)
        elif emitter in emitters:
            contents = emitters[emitter](ir)
        else:
            raise ValueError(f"Unknown code generator: {emitter}")
        files[path] = contents
    return files


def update_outputs(files, *, check):
    # Complete validation and formatting before touching any checked-in output.
    formatted = {}
    for path, contents in files.items():
        if path.suffix in (".cpp", ".hpp", ".h"):
            contents = subprocess.run(
                ["clang-format", f"--assume-filename={path}"], input=contents, text=True,
                capture_output=True, check=True,
            ).stdout
        formatted[path] = contents
    stale = []
    for path, contents in formatted.items():
        if not path.exists() or path.read_text() != contents:
            if check:
                stale.append(path.relative_to(ROOT).as_posix())
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
    return stale


def main():
    global ROOT, OUTPUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--database", type=Path, default=DATABASE,
        help="Compilation database selected by the active build configuration"
    )
    parser.add_argument(
        "--check", action="store_true", help="Fail when checked-in output differs"
    )
    parser.add_argument("--manifest", type=Path, default=OUTPUT / "codegen.json",
                        help="Repository-owned declaration roots and generated output list")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    if manifest["version"] != 1:
        raise ValueError("Code generation manifest version must be 1")
    ROOT = (args.manifest.resolve().parent / manifest["root"]).resolve()
    OUTPUT = ROOT / "source/api"
    ir = parse_api(json.loads(args.database.read_text()), manifest["sources"], manifest.get("callback_headers", []))
    files = generate_outputs(ir, manifest["outputs"], ROOT)
    stale = update_outputs(files, check=args.check)
    if stale:
        raise SystemExit("Generated files are stale: " + ", ".join(stale))
    print("Generated API is current" if args.check else "Generated API written")


if __name__ == "__main__":
    main()
