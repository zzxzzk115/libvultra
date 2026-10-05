#!/usr/bin/env python3
"""Generate the small public ABI and Inspector surface from annotated C++ declarations."""

import argparse
import json
import math
import re
import subprocess
from pathlib import Path

from clang import cindex

ROOT = Path(__file__).resolve().parents[1]
DATABASE = ROOT / ".vscode/compile_commands.json"
OUTPUT = ROOT / "source/api"
ABI_VERSION = 1
SOURCES = {
    "ui": "source/ui/src/editor_gui_frame.cpp",
    "settings": "source/servers/src/rendering/builtin/builtin_renderer.cpp",
    "scene": "source/scene/src/scene_api.cpp",
    "experiment": "source/scripting/src/experiment_host.cpp",
}


def translation_unit(database, source):
    row = next((row for row in database if row["file"].endswith(source)), None)
    if row is None:
        raise RuntimeError(
            f"compile_commands.json has no entry for {source}; run xmake first"
        )
    args = list(row["arguments"][1:])
    for option in ("-o",):
        if option in args:
            index = args.index(option)
            del args[index : index + 2]
    args = [arg for arg in args if arg not in ("-c", row["file"])]
    resource_dir = subprocess.check_output(
        ["clang", "-print-resource-dir"], text=True
    ).strip()
    args.append(f"-resource-dir={resource_dir}")
    unit = cindex.Index.create().parse(
        row["file"],
        args=args,
        options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES,
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
        if child.location.file and "/vultra/" in child.location.file.name:
            yield from walk(child)


def annotations(cursor):
    return [
        child.spelling
        for child in cursor.get_children()
        if child.kind == cindex.CursorKind.ANNOTATE_ATTR
    ]


def parse_api(database):
    ui_unit = translation_unit(database, SOURCES["ui"])
    scene_unit = translation_unit(database, SOURCES["scene"])
    settings_unit = translation_unit(database, SOURCES["settings"])
    experiment_unit = translation_unit(database, SOURCES["experiment"])
    functions = []
    for cursor in walk(ui_unit.cursor):
        if (
            cursor.kind != cindex.CursorKind.FUNCTION_DECL
            or "vultra.bind.ui" not in annotations(cursor)
        ):
            continue
        if not cursor.location.file.name.endswith("/editor_gui_frame.hpp"):
            continue
        signature = [argument.type.spelling for argument in cursor.get_arguments()]
        if signature != ["EditorGuiFrame &", "std::string_view"] or cursor.result_type.spelling not in ("bool", "void"):
            raise RuntimeError(
                f"Unsupported UI ABI signature: {cursor.spelling}({signature})"
            )
        functions.append(
            {
                "name": cursor.spelling,
                "return": cursor.result_type.spelling,
                "argument": list(cursor.get_arguments())[1].spelling,
            }
        )
    functions.sort(key=lambda item: item["name"])
    if not functions:
        raise RuntimeError("No annotated UI ABI functions")

    pods = []
    for cursor in walk(scene_unit.cursor):
        if cursor.kind != cindex.CursorKind.STRUCT_DECL or "vultra.bind.pod" not in annotations(cursor):
            continue
        fields = []
        for field in cursor.get_children():
            if field.kind != cindex.CursorKind.FIELD_DECL:
                continue
            if field.type.spelling != "float":
                raise RuntimeError(f"Unsupported ABI POD field: {cursor.spelling}.{field.spelling}: {field.type.spelling}")
            fields.append({"name": field.spelling, "type": "float"})
        if not fields or cursor.type.get_size() != len(fields) * 4 or cursor.type.get_align() != 4:
            raise RuntimeError(f"Unsupported ABI POD layout: {cursor.spelling}")
        pods.append({"name": cursor.spelling, "fields": fields})
    pods.sort(key=lambda item: item["name"])
    pod_names = {pod["name"] for pod in pods}
    scene_functions = []
    for cursor in walk(scene_unit.cursor):
        if (
            cursor.kind != cindex.CursorKind.FUNCTION_DECL
            or "vultra.bind.scene" not in annotations(cursor)
        ):
            continue
        if not cursor.location.file.name.endswith("/scene_api.hpp"):
            continue
        arguments = [
            {"name": argument.spelling, "type": argument.type.spelling}
            for argument in cursor.get_arguments()
        ]
        result = cursor.result_type.spelling
        project_required = len(arguments) > 1 and arguments[1]["type"] == "const ProjectManifest &"
        call_arguments = arguments[2:] if project_required else arguments[1:]
        supported_arguments = {"ObjectId", "uint64_t", "std::string_view"} | pod_names
        if (
            not cursor.spelling.startswith("scene")
            or not arguments
            or arguments[0]["type"] not in ("const SceneTree &", "SceneTree &")
            or any(arg["type"] not in supported_arguments for arg in call_arguments)
            or result not in {"ObjectId", "uint64_t", "std::string_view", "void"} | pod_names
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
                "arguments": call_arguments,
            }
        )
    scene_functions.sort(key=lambda item: item["name"])
    if not scene_functions:
        raise RuntimeError("No annotated scene ABI functions")

    reflected = {}
    for unit in (settings_unit, scene_unit):
        for item in parse_reflection(unit):
            name = item["name"]
            if name in reflected and reflected[name] != item:
                raise RuntimeError(f"Conflicting reflected declarations: {name}")
            reflected[name] = item
    structs = sorted(reflected.values(), key=lambda item: item["name"])
    if not any(item["name"] == "RenderSettings" for item in structs):
        raise RuntimeError("Missing reflected RenderSettings declaration")
    experiment_pods, experiment_functions = parse_experiments(experiment_unit)
    return {"ui_functions": functions, "scene_functions": scene_functions, "pods": pods, "types": structs,
            "experiment_pods": experiment_pods, "experiment_functions": experiment_functions}


def parse_experiments(unit):
    scalar_types = {"float", "double", "uint32_t", "uint64_t"}
    pods = []
    methods = []
    for cursor in walk(unit.cursor):
        if not cursor.location.file or not cursor.location.file.name.endswith("/experiment_host.hpp"):
            continue
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
        if cursor.kind == cindex.CursorKind.CXX_METHOD and "vultra.bind.experiment" in annotations(cursor):
            if cursor.semantic_parent.spelling != "ExperimentHost" or cursor.is_static_method():
                raise RuntimeError("Experiment ABI requires instance methods on ExperimentHost")
            methods.append(cursor)
    pod_names = {pod["name"] for pod in pods}
    functions = []
    for method in methods:
        arguments = [{"name": argument.spelling, "type": argument.type.spelling}
                     for argument in method.get_arguments()]
        result = method.result_type.spelling
        if result not in {"void", "uint64_t", "std::string_view"} | pod_names or any(
            argument["type"] not in scalar_types | {"std::string_view", "std::span<float>"}
            for argument in arguments
        ):
            raise RuntimeError(f"Unsupported experiment ABI signature: {method.spelling}({arguments}) -> {result}")
        functions.append({"name": method.spelling, "arguments": arguments, "return": result,
                          "host_mutable": not method.is_const_method()})
    if not pods or not functions:
        raise RuntimeError("Missing annotated experiment API")
    return sorted(pods, key=lambda item: item["name"]), sorted(functions, key=lambda item: item["name"])


def parse_reflection(unit):
    structs = []
    for cursor in walk(unit.cursor):
        if (
            cursor.kind != cindex.CursorKind.STRUCT_DECL
            or "vultra.reflect" not in annotations(cursor)
        ):
            continue
        fields = []
        for field in cursor.get_children():
            if field.kind != cindex.CursorKind.FIELD_DECL:
                continue
            metadata = next(
                (
                    a[len("vultra.property:") :]
                    for a in annotations(field)
                    if a.startswith("vultra.property:")
                ),
                None,
            )
            if metadata is None:
                continue
            properties = dict(
                part.split("=", 1) for part in metadata.split(";") if "=" in part
            )
            field_type = field.type.spelling
            if field_type not in ("bool", "float", "RenderPath"):
                raise RuntimeError(
                    f"Unsupported reflected field: {cursor.spelling}.{field.spelling}: {field_type}"
                )
            if field_type == "float" and not {"min", "max"} <= properties.keys():
                raise RuntimeError(f"Slider {field.spelling} needs min and max")
            if field_type == "RenderPath" and "options" not in properties:
                raise RuntimeError(f"Enum {field.spelling} needs options")
            if properties.get("widget", "slider") not in ("slider", "drag"):
                raise RuntimeError(f"Unsupported widget: {cursor.spelling}.{field.spelling}")
            if properties.get("widget") == "drag" and (field_type != "float" or "speed" not in properties):
                raise RuntimeError(f"Drag field {field.spelling} needs a float value and speed")
            if field_type == "float":
                try:
                    minimum, maximum = float(properties["min"]), float(properties["max"])
                    speed = float(properties.get("speed", "1"))
                except ValueError as error:
                    raise RuntimeError(f"Invalid property number: {cursor.spelling}.{field.spelling}") from error
                if (
                    not all(math.isfinite(value) for value in (minimum, maximum, speed))
                    or minimum >= maximum
                    or speed <= 0
                ):
                    raise RuntimeError(f"Invalid property range/speed: {cursor.spelling}.{field.spelling}")
            fields.append({"name": field.spelling, "type": field_type, **properties})
        if not fields:
            raise RuntimeError(f"Reflected type has no annotated properties: {cursor.spelling}")
        header = cursor.location.file.name.split("/include/", 1)[1]
        structs.append({"name": cursor.spelling, "header": header, "fields": fields})
    return structs


def common_header():
    return """/* Generated by scripts/codegen.py. Do not edit. */
#ifndef VULTRA_ABI_GENERATED_H
#define VULTRA_ABI_GENERATED_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VULTRA_ABI_VERSION __ABI_VERSION__u
typedef enum VultraStatus {
    VULTRA_STATUS_OK = 0,
    VULTRA_STATUS_INVALID_ARGUMENT = 1,
    VULTRA_STATUS_INVALID_FRAME = 2,
    VULTRA_STATUS_ERROR = 3
} VultraStatus;
#ifdef __cplusplus
}
#endif
#endif
""".replace("__ABI_VERSION__", str(ABI_VERSION))


def c_header(ir):
    declarations = []
    for function in ir["ui_functions"]:
        name = function["name"][3:].lower()
        out = ", uint8_t* changed" if function["return"] == "bool" else ""
        declarations.append(
            f"    VultraStatus (*{name})(VultraUiFrame frame, const char* text, uint64_t text_size{out});"
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
typedef struct VultraUiApi {
    uint32_t version;
    uint32_t struct_size;
"""
        + "\n".join(declarations)
        + "\n} VultraUiApi;\n#ifdef __cplusplus\n}\n#endif\n#endif\n"
    )


def c_name(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name[5:]).lower()


def scene_header(ir):
    pod_names = {pod["name"] for pod in ir["pods"]}
    pod_declarations = []
    for pod in ir["pods"]:
        fields = "\n".join(f"    float {field['name']};" for field in pod["fields"])
        pod_declarations.append(f"typedef struct Vultra{pod['name']} {{\n{fields}\n}} Vultra{pod['name']};")
    declarations = []
    for function in ir["scene_functions"]:
        parameters = ["VultraSceneFrame frame"]
        for argument in function["arguments"]:
            if argument["type"] == "std::string_view":
                parameters.extend([f"const char* {argument['name']}", f"uint64_t {argument['name']}Size"])
            else:
                kind = "Vultra" + argument["type"] if argument["type"] in pod_names else "uint64_t"
                parameters.append(f"{kind} {argument['name']}")
        if function["return"] == "std::string_view":
            parameters.extend(["const char** data", "uint64_t* size"])
        elif function["return"] in pod_names:
            parameters.append(f"Vultra{function['return']}* value")
        elif function["return"] != "void":
            parameters.append("uint64_t* value")
        declarations.append(
            f"    VultraStatus (*{c_name(function['name'])})({', '.join(parameters)});"
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
    void* context; /* Borrowed; valid only during the update callback. */
    uint64_t serial;
} VultraSceneFrame;
"""
        + "\n".join(pod_declarations)
        + """
typedef struct VultraSceneApi {
    uint32_t version;
    uint32_t struct_size;
"""
        + "\n".join(declarations)
        + "\n} VultraSceneApi;\n#ifdef __cplusplus\n}\n#endif\n#endif\n"
    )


def c_source(ir):
    wrappers = []
    table = []
    for function in ir["ui_functions"]:
        name = function["name"][3:].lower()
        is_bool = function["return"] == "bool"
        out = ", uint8_t* changed" if is_bool else ""
        check = " || !changed" if is_bool else ""
        call = (
            f"*changed = vultra::{function['name']}(current, {{text ? text : \"\", static_cast<size_t>(textSize)}});"
            if is_bool
            else f"vultra::{function['name']}(current, {{text ? text : \"\", static_cast<size_t>(textSize)}});"
        )
        wrappers.append(
            f"""static VultraStatus ui{function["name"][3:]}(VultraUiFrame frame, const char* text, uint64_t textSize{out})
{{
    if (!frame.context || (!text && textSize != 0){check})
    {{
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }}
    auto* gui = static_cast<vultra::EditorGui*>(frame.context);
    if (!gui->frameActive() || gui->frameSerial() != frame.serial)
    {{
        return VULTRA_STATUS_INVALID_FRAME;
    }}
    try
    {{
        auto current = gui->frame();
        {call}
        return VULTRA_STATUS_OK;
    }}
    catch (...)
    {{
        return VULTRA_STATUS_ERROR;
    }}
}}"""
        )
        table.append(f"ui{function['name'][3:]}")
    return (
        """// Generated by scripts/codegen.py. Do not edit.
#include <vultra/api/ui_bridge.hpp>
#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_frame.hpp>

#include <cstddef>

"""
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
        static const VultraUiApi api {{VULTRA_ABI_VERSION, sizeof(VultraUiApi), {', '.join(table)}}};
        return api;
    }}
}} // namespace vultra
"""
    )


def scene_source(ir):
    pods = {pod["name"]: pod["fields"] for pod in ir["pods"]}
    wrappers = []
    table = []
    for function in ir["scene_functions"]:
        parameters = ["VultraSceneFrame frame"]
        calls = ["*access->scene"]
        if function["project_required"]:
            calls.append("*access->project")
        text_checks = []
        for argument in function["arguments"]:
            name = argument["name"]
            kind = argument["type"]
            if kind == "std::string_view":
                parameters.extend([f"const char* {name}", f"uint64_t {name}Size"])
                calls.append(f"std::string_view {{{name} ? {name} : \"\", static_cast<size_t>({name}Size)}}")
                text_checks.append(f"(!{name} && {name}Size != 0)")
            else:
                parameters.append(f"{'Vultra' + kind if kind in pods else 'uint64_t'} {name}")
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
        elif result in pods:
            parameters.append(f"Vultra{result}* value")
            check = "!value"
        elif result == "void":
            check = None
        else:
            parameters.append("uint64_t* value")
            check = "!value"
        expression = f"vultra::{function['name']}({', '.join(calls)})"
        if result == "std::string_view":
            assign = f"const auto result = {expression};\n        *data = result.data();\n        *size = result.size();"
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
            f"""static VultraStatus {wrapper_name}({', '.join(parameters)})
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
#include <vultra/scene/scene_api.hpp>

#include <cstddef>
#include <stdexcept>
#include <string_view>

"""
        + "\n\n".join(wrappers)
        + f"""

namespace vultra
{{
    const VultraSceneApi& sceneApi()
    {{
        static const VultraSceneApi api {{VULTRA_ABI_VERSION, sizeof(VultraSceneApi), {', '.join(table)}}};
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
        name, kind = argument["name"], argument["type"]
        if kind == "std::string_view":
            parameters.extend([(name, "const char*"), (name + "Size", "uint64_t")])
        elif kind == "std::span<float>":
            parameters.extend([(name, "float*"), (name + "Count", "uint64_t")])
        else:
            parameters.append((name, kind))
    result = function["return"]
    if result == "std::string_view":
        parameters.extend([("data", "const char**"), ("size", "uint64_t*")])
    elif result in pod_names:
        parameters.append(("value", f"Vultra{result}*"))
    elif result != "void":
        parameters.append(("value", result + "*"))
    return parameters


def experiment_header(ir):
    pod_names = {pod["name"] for pod in ir["experiment_pods"]}
    structs = []
    for pod in ir["experiment_pods"]:
        fields = "\n".join(f"    {field['type']} {field['name']};" for field in pod["fields"])
        structs.append(f"typedef struct Vultra{pod['name']} {{\n{fields}\n}} Vultra{pod['name']};")
    declarations = []
    for function in ir["experiment_functions"]:
        parameters = ", ".join(f"{kind} {name}" for name, kind in experiment_parameters(function, pod_names))
        declarations.append(f"    VultraStatus (*{experiment_name(function['name'])})({parameters});")
    return """/* Generated by scripts/codegen.py. Do not edit. */
#ifndef VULTRA_EXPERIMENT_GENERATED_H
#define VULTRA_EXPERIMENT_GENERATED_H
#include <vultra/api/vultra_abi.generated.h>
#ifdef __cplusplus
extern "C" {
#endif
""" + "\n".join(structs) + """
/* Opaque context is borrowed from the host. Calls must be serialized; no C++ exceptions cross this table. */
typedef struct VultraExperimentApi {
    uint32_t version;
    uint32_t struct_size;
""" + "\n".join(declarations) + "\n} VultraExperimentApi;\n#ifdef __cplusplus\n}\n#endif\n#endif\n"


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
        wrappers.append(f"""static VultraStatus {wrapper}({parameters})
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
#include <vultra/api/experiment_bridge.hpp>
#include <vultra/scripting/experiment_host.hpp>

#include <cstddef>
#include <stdexcept>

""" + "\n".join(assertions) + "\n\n" + "\n\n".join(wrappers) + f"""

namespace vultra
{{
    const VultraExperimentApi& experimentApi()
    {{
        static const VultraExperimentApi api {{VULTRA_ABI_VERSION, sizeof(VultraExperimentApi), {', '.join(table)}}};
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


def property_metadata_header():
    return """// Generated by scripts/codegen.py. Do not edit.
#pragma once
namespace vultra
{
    enum class ReflectedPropertyKind { eBool, eFloat, eEnum };
    struct ReflectedProperty
    {
        const char* name;
        const char* label;
        ReflectedPropertyKind kind;
        float min;
        float max;
    };
} // namespace vultra
"""


def reflection_header(types):
    headers = sorted({item["header"] for item in types})
    result = "// Generated by scripts/codegen.py. Do not edit.\n#pragma once\n"
    result += "#include <vultra/api/property_metadata.generated.hpp>\n"
    result += "\n".join(f"#include <{header}>" for header in headers)
    result += "\n#include <vultra/ui/editor_gui_inspector.hpp>\n\n#include <span>\n"
    result += "\nnamespace vultra\n{\n"
    for item in types:
        name = item["name"]
        function = name[0].lower() + name[1:]
        result += f"    std::span<const ReflectedProperty> {function}Properties();\n"
        result += f"    bool draw{name}(EditorGuiInspector& inspector, {name}& settings);\n"
    return result + "} // namespace vultra\n"


def reflection_type_source(item):
    fields = item["fields"]
    type_name = item["name"]
    function = type_name[0].lower() + type_name[1:]
    props = []
    draw = []
    for field in fields:
        name, label, kind = (
            field["name"],
            field.get("label", field["name"]),
            field["type"],
        )
        enum = {"bool": "eBool", "float": "eFloat", "RenderPath": "eEnum"}[kind]
        min_value, max_value = field.get("min", "0"), field.get("max", "0")
        # Preserve the existing RenderSettings drawer keys; scene keys identify their owning type.
        property_id = name if type_name == "RenderSettings" else f"{type_name}.{name}"
        props.append(
            f'        {{"{name}", "{label}", ReflectedPropertyKind::{enum}, {min_value}, {max_value}}},'
        )
        if kind == "bool":
            draw.append(
                f'    changed |= inspector.boolField({{"{property_id}", "{label}"}}, &settings.{name});'
            )
        elif kind == "float":
            if field.get("widget") == "drag":
                draw.append(
                    f'    changed |= inspector.floatField({{"{property_id}", "{label}"}}, '
                    f'&settings.{name}, {field["speed"]}, {min_value}, {max_value});'
                )
            else:
                draw.append(
                    f'    changed |= inspector.floatSlider({{"{property_id}", "{label}"}}, '
                    f'&settings.{name}, {min_value}, {max_value});'
                )
        else:
            options = field["options"].split("|")
            option_names = ", ".join(f'"{option}"' for option in options)
            selected = f"static_cast<int>(settings.{name})"
            draw.append(f"""    int selected = {selected};
    const char* const options[] = {{{option_names}}};
    if (inspector.choice({{"{property_id}", "{label}"}}, &selected, options, {len(options)}))
    {{
        settings.{name} = static_cast<RenderPath>(selected);
        changed = true;
    }}""")
    return (
        f"""    std::span<const ReflectedProperty> {function}Properties()
    {{
        static constexpr std::array<ReflectedProperty, """
        + str(len(fields))
        + """> properties = {{
"""
        + "\n".join(props)
        + """
        }};
        return properties;
    }

    bool draw__TYPE__(EditorGuiInspector& inspector, __TYPE__& settings)
    {
        bool changed = false;
"""
        + "\n".join(draw)
        + "\n        return changed;\n    }\n"
    ).replace("__TYPE__", type_name)


def reflection_source(types, header):
    result = f"// Generated by scripts/codegen.py. Do not edit.\n#include <vultra/api/{header}>\n"
    result += "\n#include <array>\n\nnamespace vultra\n{\n"
    return result + "\n".join(reflection_type_source(item) for item in types) + "} // namespace vultra\n"



def csharp_bindings(ir):
    pods = {pod["name"]: pod["fields"] for pod in ir["pods"]}
    pod_declarations = []
    for name, fields in pods.items():
        members = "\n".join(f"    public float {field['name'][0].upper() + field['name'][1:]};" for field in fields)
        pod_declarations.append(f"[StructLayout(LayoutKind.Sequential)]\ninternal struct Vultra{name}\n{{\n{members}\n}}")
    ui_fields = []
    for function in ir["ui_functions"]:
        name = function["name"][3:]
        output = ", byte*, VultraStatus" if function["return"] == "bool" else ", VultraStatus"
        ui_fields.append(
            f"    public delegate* unmanaged[Cdecl]<VultraUiFrame, byte*, ulong{output}> {name};"
        )
    scene_fields = []
    for function in ir["scene_functions"]:
        name = function["name"][5:]
        argument_types = {
            "std::string_view": "byte*, ulong",
            "ObjectId": "ulong",
            "uint64_t": "ulong",
        }
        argument_types.update({name: "Vultra" + name for name in pods})
        arguments = ", ".join(argument_types[argument["type"]] for argument in function["arguments"])
        if arguments:
            arguments += ", "
        output = {
            "std::string_view": "byte**, ulong*, VultraStatus",
            "void": "VultraStatus",
        }
        output.update({name: f"Vultra{name}*, VultraStatus" for name in pods})
        output = output.get(function["return"], "ulong*, VultraStatus")
        scene_fields.append(
            f"    public delegate* unmanaged[Cdecl]<VultraSceneFrame, {arguments}{output}> {name};"
        )
    return """// Generated by scripts/codegen.py. Do not edit.
using System.Runtime.InteropServices;

namespace Vultra.Interop;

internal static class VultraAbi
{
    public const uint Version = __ABI_VERSION__;
}

internal enum VultraStatus : int
{
    Ok = 0,
    InvalidArgument = 1,
    InvalidFrame = 2,
    Error = 3
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
""".replace("__ABI_VERSION__", str(ABI_VERSION)) + "\n".join(ui_fields) + """
}

""" + "\n\n".join(pod_declarations) + "\n\n" + """
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct VultraSceneApi
{
    public uint Version;
    public uint StructSize;
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
}
"""



def lua_values(ir):
    """Marshal the same annotated PODs used by the C ABI into Lua value tables."""
    result = """// Generated by scripts/codegen.py. Do not edit.
#pragma once

#include <vultra/api/vultra_scene.generated.h>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace vultra::scripting::detail
{
    inline float luaFloatField(lua_State* state, int index, const char* name)
    {
        lua_getfield(state, index, name);
        const auto value = static_cast<float>(luaL_checknumber(state, -1));
        lua_pop(state, 1);
        return value;
    }

"""
    for pod in ir["pods"]:
        name = "Vultra" + pod["name"]
        result += f"    inline void pushLuaValue(lua_State* state, const {name}& value)\n    {{\n        lua_newtable(state);\n"
        for field in pod["fields"]:
            key = re.sub(r"(?<!^)(?=[A-Z])", "_", field["name"]).lower()
            result += f'        lua_pushnumber(state, value.{field["name"]});\n        lua_setfield(state, -2, "{key}");\n'
        result += "    }\n\n"
        result += f"    inline {name} readLua{pod['name']}(lua_State* state, int index)\n    {{\n        luaL_checktype(state, index, LUA_TTABLE);\n        index = lua_absindex(state, index);\n        return {{\n"
        for field in pod["fields"]:
            key = re.sub(r"(?<!^)(?=[A-Z])", "_", field["name"]).lower()
            result += f'            luaFloatField(state, index, "{key}"),\n'
        result += "        };\n    }\n\n"
    return result + "} // namespace vultra::scripting::detail\n"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="Fail when checked-in output differs"
    )
    args = parser.parse_args()
    ir = parse_api(json.loads(DATABASE.read_text()))
    render_types = [item for item in ir["types"] if item["name"] == "RenderSettings"]
    scene_types = [item for item in ir["types"] if item["name"] != "RenderSettings"]
    files = {
        OUTPUT / "ir/api.json": json.dumps(ir, indent=2, sort_keys=True) + "\n",
        OUTPUT / "csharp/VultraBindings.g.cs": csharp_bindings(ir),
        ROOT / "source/scripting/include/vultra/scripting/lua_values.generated.hpp": lua_values(ir),
        OUTPUT / "include/vultra/api/vultra_abi.generated.h": common_header(),
        OUTPUT / "include/vultra/api/vultra_ui.generated.h": c_header(ir),
        OUTPUT / "include/vultra/api/vultra_scene.generated.h": scene_header(ir),
        OUTPUT / "src/vultra_ui.generated.cpp": c_source(ir),
        OUTPUT / "src/vultra_scene.generated.cpp": scene_source(ir),
        OUTPUT / "include/vultra/api/vultra_experiment.generated.h": experiment_header(ir),
        ROOT / "source/scripting/src/experiment_api.generated.cpp": experiment_source(ir),
        OUTPUT / "python/vultra/_bindings_generated.py": python_experiment_bindings(ir),
        OUTPUT
        / "include/vultra/api/render_settings.generated.hpp": reflection_header(render_types),
        OUTPUT / "src/render_settings.generated.cpp": reflection_source(render_types, "render_settings.generated.hpp"),
        OUTPUT / "include/vultra/api/property_metadata.generated.hpp": property_metadata_header(),
        OUTPUT / "include/vultra/api/scene_properties.generated.hpp": reflection_header(scene_types),
        OUTPUT / "src/scene_properties.generated.cpp": reflection_source(scene_types, "scene_properties.generated.hpp"),
    }
    stale = []
    for path, contents in files.items():
        if path.suffix in (".cpp", ".hpp", ".h"):
            contents = subprocess.run(
                ["clang-format"],
                input=contents,
                text=True,
                capture_output=True,
                check=True,
            ).stdout
        if not path.exists() or path.read_text() != contents:
            if args.check:
                stale.append(path.relative_to(ROOT).as_posix())
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
    if stale:
        raise SystemExit("Generated files are stale: " + ", ".join(stale))
    print("Generated API is current" if args.check else "Generated API written")


if __name__ == "__main__":
    main()
