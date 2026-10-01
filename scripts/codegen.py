#!/usr/bin/env python3
"""Generate the small public ABI and Inspector surface from annotated C++ declarations."""

import argparse
import json
import re
import subprocess
from pathlib import Path

from clang import cindex

ROOT = Path(__file__).resolve().parents[1]
DATABASE = ROOT / ".vscode/compile_commands.json"
OUTPUT = ROOT / "source/api"
ABI_VERSION = 3
SOURCES = {
    "ui": "source/ui/src/editor_gui_frame.cpp",
    "settings": "source/servers/src/rendering/builtin/builtin_renderer.cpp",
    "scene": "source/scene/src/scene_api.cpp",
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
        if (
            not cursor.spelling.startswith("scene")
            or not arguments
            or arguments[0]["type"] not in ("const SceneTree &", "SceneTree &")
            or any(arg["type"] not in ("ObjectId", "uint64_t", "SceneTranslation") for arg in arguments[1:])
            or result not in ("ObjectId", "uint64_t", "std::string_view", "SceneTranslation", "void")
        ):
            raise RuntimeError(
                f"Unsupported scene ABI signature: {cursor.spelling}({arguments}) -> {result}"
            )
        scene_functions.append(
            {"name": cursor.spelling, "return": result, "scene_mutable": arguments[0]["type"] == "SceneTree &", "arguments": arguments[1:]}
        )
    scene_functions.sort(key=lambda item: item["name"])
    if not scene_functions:
        raise RuntimeError("No annotated scene ABI functions")

    structs = []
    for cursor in walk(settings_unit.cursor):
        if (
            cursor.kind != cindex.CursorKind.STRUCT_DECL
            or "vultra.reflect" not in annotations(cursor)
        ):
            continue
        if not cursor.location.file.name.endswith("/builtin_renderer.hpp"):
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
            fields.append({"name": field.spelling, "type": field_type, **properties})
        structs.append({"name": cursor.spelling, "fields": fields})
    if len(structs) != 1 or structs[0]["name"] != "RenderSettings":
        raise RuntimeError("Expected exactly one reflected RenderSettings declaration")
    return {"ui_functions": functions, "scene_functions": scene_functions, "types": structs}


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
    declarations = []
    for function in ir["scene_functions"]:
        parameters = ["VultraSceneFrame frame"]
        parameters.extend(
            f"{'VultraSceneTranslation' if argument['type'] == 'SceneTranslation' else 'uint64_t'} {argument['name']}"
            for argument in function["arguments"]
        )
        if function["return"] == "std::string_view":
            parameters.extend(["const char** data", "uint64_t* size"])
        elif function["return"] == "SceneTranslation":
            parameters.append("VultraSceneTranslation* value")
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
typedef struct VultraSceneTranslation {
    float x;
    float y;
    float z;
} VultraSceneTranslation;
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
    wrappers = []
    table = []
    for function in ir["scene_functions"]:
        parameters = ["VultraSceneFrame frame"]
        calls = ["*access->scene"]
        for argument in function["arguments"]:
            name = argument["name"]
            kind = argument["type"]
            parameters.append(
                f"{'VultraSceneTranslation' if kind == 'SceneTranslation' else 'uint64_t'} {name}"
            )
            calls.append(
                f"vultra::ObjectId {{{name}}}"
                if kind == "ObjectId"
                else f"vultra::SceneTranslation {{{name}.x, {name}.y, {name}.z}}"
                if kind == "SceneTranslation"
                else name
            )
        result = function["return"]
        if result == "std::string_view":
            parameters.extend(["const char** data", "uint64_t* size"])
            check = "!data || !size"
        elif result == "SceneTranslation":
            parameters.append("VultraSceneTranslation* value")
            check = "!value"
        elif result == "void":
            check = None
        else:
            parameters.append("uint64_t* value")
            check = "!value"
        expression = f"vultra::{function['name']}({', '.join(calls)})"
        if result == "std::string_view":
            assign = f"const auto result = {expression};\n        *data = result.data();\n        *size = result.size();"
        elif result == "SceneTranslation":
            assign = f"const auto result = {expression};\n        *value = {{result.x, result.y, result.z}};"
        elif result == "void":
            assign = f"{expression};"
        else:
            assign = f"*value = {expression}" + (".value;" if result == "ObjectId" else ";")
        condition = "!frame.context" + (f" || {check}" if check else "")
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
    try
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

#include <stdexcept>

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


def reflection_header(ir):
    return """// Generated by scripts/codegen.py. Do not edit.
#pragma once
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/ui/editor_gui_inspector.hpp>

#include <span>

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
    std::span<const ReflectedProperty> renderSettingsProperties();
    bool drawRenderSettings(EditorGuiInspector& inspector, RenderSettings& settings);
} // namespace vultra
"""


def reflection_source(ir):
    fields = ir["types"][0]["fields"]
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
        props.append(
            f'        {{"{name}", "{label}", ReflectedPropertyKind::{enum}, {min_value}, {max_value}}},'
        )
        if kind == "bool":
            draw.append(
                f'    changed |= inspector.boolField({{"{name}", "{label}"}}, &settings.{name});'
            )
        elif kind == "float":
            draw.append(
                f'    changed |= inspector.floatSlider({{"{name}", "{label}"}}, &settings.{name}, {min_value}, {max_value});'
            )
        else:
            options = field["options"].split("|")
            option_names = ", ".join(f'"{option}"' for option in options)
            selected = (
                " : ".join(
                    f"settings.{name} == RenderPath::e{option} ? {index}"
                    for index, option in enumerate(options[:-1])
                )
                + f" : {len(options)-1}"
            )
            draw.append(f"""    int selected = {selected};
    const char* const options[] = {{{option_names}}};
    if (inspector.choice({{"{name}", "{label}"}}, &selected, options, {len(options)}))
    {{
        settings.{name} = static_cast<RenderPath>(selected);
        changed = true;
    }}""")
    return (
        """// Generated by scripts/codegen.py. Do not edit.
#include <vultra/api/render_settings.generated.hpp>

#include <array>

namespace vultra
{
    std::span<const ReflectedProperty> renderSettingsProperties()
    {
        static constexpr std::array<ReflectedProperty, """
        + str(len(fields))
        + """> properties = {{
"""
        + "\n".join(props)
        + """
        }};
        return properties;
    }

    bool drawRenderSettings(EditorGuiInspector& inspector, RenderSettings& settings)
    {
        bool changed = false;
"""
        + "\n".join(draw)
        + "\n        return changed;\n    }\n} // namespace vultra\n"
    )



def csharp_bindings(ir):
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
        arguments = ", ".join(
            "VultraSceneTranslation" if argument["type"] == "SceneTranslation" else "ulong"
            for argument in function["arguments"]
        )
        if arguments:
            arguments += ", "
        output = {
            "std::string_view": "byte**, ulong*, VultraStatus",
            "SceneTranslation": "VultraSceneTranslation*, VultraStatus",
            "void": "VultraStatus",
        }.get(function["return"], "ulong*, VultraStatus")
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

[StructLayout(LayoutKind.Sequential)]
internal struct VultraSceneTranslation
{
    public float X;
    public float Y;
    public float Z;
}

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="Fail when checked-in output differs"
    )
    args = parser.parse_args()
    ir = parse_api(json.loads(DATABASE.read_text()))
    files = {
        OUTPUT / "ir/api.json": json.dumps(ir, indent=2, sort_keys=True) + "\n",
        OUTPUT / "csharp/VultraBindings.g.cs": csharp_bindings(ir),
        OUTPUT / "include/vultra/api/vultra_abi.generated.h": common_header(),
        OUTPUT / "include/vultra/api/vultra_ui.generated.h": c_header(ir),
        OUTPUT / "include/vultra/api/vultra_scene.generated.h": scene_header(ir),
        OUTPUT / "src/vultra_ui.generated.cpp": c_source(ir),
        OUTPUT / "src/vultra_scene.generated.cpp": scene_source(ir),
        OUTPUT
        / "include/vultra/api/render_settings.generated.hpp": reflection_header(ir),
        OUTPUT / "src/render_settings.generated.cpp": reflection_source(ir),
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
