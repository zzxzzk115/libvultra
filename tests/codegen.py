"""Tool-side reflection checks; requires the same Python/libclang setup as xmake codegen."""

from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

from clang import cindex

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import codegen


def declaration(name="Probe", kind="float", metadata="label=Gain;min=0;max=2"):
    aliases = "using int64_t = long long;" if kind == "int64_t" else ""
    return (
        aliases +
        f'namespace vultra {{ struct [[clang::annotate("vultra.reflect")]] {name} {{ '
        f'[[clang::annotate("vultra.property:{metadata}")]] {kind} value; '
        'int unannotated; }; }'
    )


def unit(source, header="reflection_probe.hpp"):
    filename = str(codegen.ROOT / "build/.tmp/include/vultra" / header)
    unit = cindex.Index.create().parse(
        filename, args=["-x", "c++", "-std=c++23"], unsaved_files=[(filename, source)]
    )
    errors = [str(item) for item in unit.diagnostics if item.severity >= cindex.Diagnostic.Error]
    if errors:
        raise AssertionError("\n".join(errors))
    return unit


def parse(source):
    return codegen.parse_reflection(unit(source))


class CompileCommandTests(unittest.TestCase):
    def test_relative_paths_use_each_compile_command_directory(self):
        with tempfile.TemporaryDirectory(dir=codegen.ROOT / "build/.tmp") as directory:
            project = Path(directory)
            include = project / "include/vultra"
            include.mkdir(parents=True)
            (include / "probe.hpp").write_text(declaration("CorrectProject"), encoding="utf-8")
            (project / "probe.cpp").write_text("#include <vultra/probe.hpp>\n", encoding="utf-8")
            command = {"directory": str(project), "file": "probe.cpp",
                       "arguments": ["clang", "-Iinclude", "-std=c++23", "-c", "probe.cpp"]}
            parsed = codegen.translation_unit([command], "probe.cpp")
            self.assertEqual([item["name"] for item in codegen.parse_reflection(parsed)], ["CorrectProject"])

    def test_scene_c_names_preserve_word_boundaries(self):
        self.assertEqual(codegen.c_name("sceneRandomUInt"), "random_uint")
        self.assertEqual(codegen.c_name("sceneRandomBoundedUInt"), "random_bounded_uint")
        self.assertEqual(codegen.c_name("sceneMenuInterval"), "menu_interval")
        self.assertEqual(codegen.c_name("sceneCreateNode3D"), "create_node3d")

    def test_declarations_regenerate_without_stale_layout_assertions(self):
        with tempfile.TemporaryDirectory(dir=codegen.ROOT / "build/.tmp") as directory:
            root = Path(directory)
            header = root / "include/vultra/reflection_probe.hpp"
            header.parent.mkdir(parents=True)
            header.write_text(declaration(), encoding="utf-8")
            source = root / "command_probe.cpp"
            source.write_text('#include "include/vultra/reflection_probe.hpp"\nstatic_assert(false, "stale layout");\n', encoding="utf-8")
            command = {"file": source.as_posix(),
                       "arguments": ["clang", "-c", "-std=c++23", source.as_posix()]}
            with self.assertRaisesRegex(RuntimeError, "stale layout"):
                codegen.translation_unit([command], "command_probe.cpp")
            parsed = codegen.translation_unit([command], "command_probe.cpp", declaration=header)
            self.assertEqual([item["name"] for item in codegen.parse_reflection(parsed)], ["Probe"])

    def test_cl_and_gnu_commands_read_the_same_annotations(self):
        with tempfile.TemporaryDirectory(dir=codegen.ROOT / "build/.tmp") as directory:
            root = Path(directory)
            include = root / "include"
            header = include / "vultra/reflection_probe.hpp"
            header.parent.mkdir(parents=True)
            header.write_text(declaration(), encoding="utf-8")
            source = root / "command_probe.cpp"
            source.write_text("#include <vultra/reflection_probe.hpp>\n", encoding="utf-8")
            commands = (
                {
                    "file": str(source).replace("/", "\\"),
                    "arguments": ["C:\\LLVM\\bin\\clang-cl.exe", "/c", "/std:c++latest",
                                  "-imsvc", str(include), str(source).replace("/", "\\")],
                },
                {
                    "file": source.as_posix(),
                    "arguments": ["clang", "-c", "-std=c++23", "-isystem", str(include),
                                  "-o", str(root / "probe.o"), source.as_posix()],
                },
            )
            for command in commands:
                with self.subTest(compiler=command["arguments"][0]):
                    with patch("codegen.subprocess.check_output", return_value=str(root)):
                        parsed = codegen.translation_unit([command], "command_probe.cpp")
                    types = codegen.parse_reflection(parsed)
                    self.assertEqual([item["name"] for item in types], ["Probe"])
                    self.assertEqual(types[0]["header"], "vultra/reflection_probe.hpp")
                    self.assertEqual(types[0]["fields"][0]["label"], "Gain")


class SceneAbiTests(unittest.TestCase):
    def test_mixed_fields_keep_native_offsets_and_integer_marshalling(self):
        source = """using uint64_t = unsigned long long; using uint32_t = unsigned int;
namespace vultra { struct [[clang::annotate("vultra.bind.pod")]] Event {
uint64_t serial; uint32_t kind; float x;
}; }"""
        pods = codegen.parse_pods(unit(source))
        self.assertEqual(pods[0]["size"], 16)
        self.assertEqual(pods[0]["alignment"], 8)
        self.assertEqual([field["offset"] for field in pods[0]["fields"]], [0, 8, 12])
        ir = {"pods": pods, "types": [], "enums": [], "ui_functions": [], "scene_functions": [], "callback_tables": []}
        self.assertIn("uint64_t serial;", codegen.scene_header(ir))
        self.assertIn("public ulong Serial;", codegen.csharp_bindings(ir))
        self.assertIn("public uint Kind;", codegen.csharp_bindings(ir))
        self.assertIn("offsetof(VultraEvent, kind) == 8", codegen.scene_source(ir))
        self.assertIn("lua_pushinteger(state, value.serial)", codegen.lua_values(ir))
        self.assertIn("Value exceeds Lua integer range: serial", codegen.lua_values(ir))
        self.assertNotIn("lua_pushnumber(state, value.serial)", codegen.lua_values(ir))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI POD"):
            codegen.parse_pods(unit(source.replace("}; }", "} __attribute__((packed)); }")))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI POD"):
            codegen.parse_pods(unit(source.replace("float x;", "double x;")))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI POD"):
            codegen.parse_pods(unit(source.replace("uint64_t serial;", "private: uint64_t serial;")))

    def test_enums_share_native_c_managed_and_lua_values(self):
        source = """using uint32_t = unsigned int;
namespace vultra { enum class [[clang::annotate("vultra.bind.enum")]] Mode : uint32_t {
eKey = 0, eMouse = 8, eText = 11
}; }"""
        enums = codegen.parse_enums(unit(source))
        self.assertEqual(enums[0]["values"][2], {"name": "Text", "value": 11})
        ir = {"enums": enums, "pods": [], "types": [], "scene_functions": []}
        self.assertIn("VULTRA_MODE_MOUSE = 8", codegen.scene_header(ir))
        self.assertIn("public enum Mode : uint", codegen.csharp_values(ir))
        self.assertIn("Text = 11", codegen.csharp_values(ir))
        self.assertIn('lua_setglobal(state, "Mode")', codegen.lua_values(ir))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI enum"):
            codegen.parse_enums(unit(source.replace("uint32_t {", "int {")))


class CallbackTableTests(unittest.TestCase):
    def test_registration_uses_generated_table_pointers_and_opaque_state(self):
        source = '''using uint32_t = unsigned int;
using uint64_t = unsigned long long;
enum VultraStatus { Ok };
struct VultraSceneFrame { void* context; uint64_t serial; };
namespace vultra {
struct [[clang::annotate("vultra.abi_table")]] TypeApi {
    uint32_t version;
    uint32_t structSize;
    VultraStatus (*create)(void*, const char*, uint64_t, void**);
    VultraStatus (*event)(void*, VultraSceneFrame, uint64_t, uint32_t, double);
};
struct [[clang::annotate("vultra.abi_table")]] RegistrationApi {
    uint32_t version;
    uint32_t structSize;
    void* context;
    VultraStatus (*add)(void*, const TypeApi*);
}; }'''
        ir = {"callback_tables": codegen.parse_callback_tables(unit(source))}
        header = codegen.callback_header(ir)
        self.assertIn("typedef struct VultraTypeApi VultraTypeApi;", header)
        self.assertIn("VultraStatus (VULTRA_ABI_CALL *add)(void*, const VultraTypeApi*)", header)
        self.assertIn("VultraStatus (VULTRA_ABI_CALL *create)(void*, const char*, uint64_t, void**)", header)
        managed = codegen.callback_csharp(ir)
        self.assertIn("public void* Context", managed)
        self.assertIn("delegate* unmanaged[Cdecl]<void*, VultraTypeApi*, VultraStatus>", managed)
        self.assertIn("VultraSceneFrame, ulong, uint, double, VultraStatus>", managed)
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI callback"):
            codegen.parse_callback_tables(unit(source.replace("const TypeApi*", "const double*")))

    def test_tables_share_c_and_managed_layouts(self):
        source = '''using uint32_t = unsigned int;
using uint64_t = unsigned long long;
enum VultraStatus { Ok };
struct VultraSceneFrame { void* context; uint64_t serial; };
namespace vultra {
struct [[clang::annotate("vultra.abi_table")]] ProbeApi {
    uint32_t version;
    uint32_t structSize;
    VultraStatus (*read)(void*, VultraSceneFrame, const char*, uint64_t);
}; }'''
        tables = codegen.parse_callback_tables(unit(source))
        self.assertEqual(tables[0]["fields"][2]["arguments"],
                         ["void*", "VultraSceneFrame", "const char*", "uint64_t"])
        ir = {"callback_tables": tables}
        self.assertIn("uint32_t struct_size", codegen.callback_header(ir))
        self.assertIn("VultraStatus (VULTRA_ABI_CALL *read)(void*, VultraSceneFrame, const char*, uint64_t)",
                      codegen.callback_header(ir))
        self.assertIn("delegate* unmanaged[Cdecl]<void*, VultraSceneFrame, byte*, ulong, VultraStatus>",
                      codegen.callback_csharp(ir))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI callback"):
            codegen.parse_callback_tables(unit(source.replace("VultraStatus (*read)", "int (*read)")))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI callback"):
            codegen.parse_callback_tables(unit(source.replace("const char*", "double*")))
        with self.assertRaisesRegex(RuntimeError, "version/structSize"):
            codegen.parse_callback_tables(unit(source.replace("uint32_t structSize;", "")))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI callback"):
            codegen.parse_callback_tables(unit(source.replace("const char*, uint64_t);", "const char*, uint64_t, ...);")))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI table layout"):
            codegen.parse_callback_tables(unit(source.replace("uint32_t version;", "virtual void extra(); uint32_t version;")))
        with self.assertRaisesRegex(RuntimeError, "Unsupported ABI table layout"):
            codegen.parse_callback_tables(unit(source.replace("}; }", "} __attribute__((packed)); }")))


class ReflectionTests(unittest.TestCase):
    def test_reference_categories_come_from_the_id_attribute(self):
        source = '''namespace vultra {
struct StableId {};
struct [[clang::annotate("vultra.reference:document")]] ItemKey { StableId value; };
struct [[clang::annotate("vultra.reflect")]] Selection {
    [[clang::annotate("vultra.property:reference=Page;flags=serialize|bind")]] ItemKey selected;
}; }'''
        types = parse(source)
        self.assertEqual(types[0]["fields"][0]["reference_category"], "document")
        self.assertIn('"document", "Page", true', codegen.property_source(types, "vultra/probe.hpp"))
        with self.assertRaisesRegex(RuntimeError, "public StableId"):
            parse(source.replace("StableId value;", "int value;"))

    def test_component_fields_compose_value_schemas(self):
        source = '''namespace vultra {
struct [[clang::annotate("vultra.reflect")]] Settings {
    [[clang::annotate("vultra.property:min=0;max=2")]] float gain = 0.5f;
};
struct [[clang::annotate("vultra.reflect.component")]] Component {
    [[clang::annotate("vultra.property:flatten=true;json=/settings")]] Settings settings{1.25f};
};
struct [[clang::annotate("vultra.reflect.component")]] EngineOwned {};
}'''
        types = parse(source)
        field = types[1]["fields"][0]
        self.assertEqual(types[1]["category"], "eComponent")
        self.assertEqual(field["name"], "settings.gain")
        self.assertEqual(field["json_path"], "/settings/gain")
        generated = codegen.property_source(types, "vultra/probe.hpp")
        self.assertIn("Component{}.settings.gain", generated)
        self.assertIn("static_cast<Component*>(object)->settings.gain =", generated)
        self.assertEqual(types[2]["fields"], [])
        with self.assertRaisesRegex(RuntimeError, "Flatten requires"):
            parse(source.replace('flatten=true;json=/settings', 'flatten=false;json=/settings'))
        with self.assertRaisesRegex(RuntimeError, "Flatten requires"):
            parse(source.replace('flatten=true;json=/settings', 'flatten=true;label=Settings'))
        with self.assertRaisesRegex(RuntimeError, "Duplicate/overlapping"):
            parse(source.replace('Settings settings{1.25f};', '''Settings settings{1.25f};
    [[clang::annotate("vultra.property:min=0;max=2;json=/settings/gain")]] float duplicate = 1;'''))

    def test_resource_categories_and_empty_identity_bases(self):
        source = '''namespace vultra {
class [[clang::annotate("vultra.reflect.resource")]] Resource {
public:
    Resource(int id);
};
class [[clang::annotate("vultra.reflect.resource")]] Settings : public Resource {
public:
    [[clang::annotate("vultra.property:set=setGain;default=1.0f;min=0;max=2")]] float gain() const;
    void setGain(float value);
}; }'''
        types = parse(source)
        self.assertEqual([item["category"] for item in types], ["eResource", "eResource"])
        self.assertEqual(types[0]["fields"], [])
        self.assertEqual(types[1]["base"], "Resource")
        generated = codegen.property_source(types, "vultra/probe.hpp")
        self.assertIn('nullptr, "resource"', generated)
        self.assertIn('&resourceType(), "resource"', generated)
        self.assertIn("std::array<PropertyInfo, 0>", generated)
        with self.assertRaisesRegex(RuntimeError, "category differs"):
            parse(source.replace('resource")]] Settings', 'node")]] Settings'))
        with self.assertRaisesRegex(RuntimeError, "no annotated properties"):
            parse('namespace vultra { struct [[clang::annotate("vultra.reflect")]] Empty {}; }')

    def test_collection_accessors_compile_with_project_arguments(self):
        database = json.loads((codegen.ROOT / "compile_commands.json").read_text())
        source_root = "source/core/src/base/property.cpp"
        original = next(row for row in database if codegen.source_path(row["file"]).endswith(source_root))
        with tempfile.TemporaryDirectory(dir=codegen.ROOT / "build/.tmp") as directory:
            root = Path(directory)
            include = root / "include"
            header = include / "vultra/collection_probe.hpp"
            header.parent.mkdir(parents=True)
            header.write_text('''#include <vultra/core/base/property.hpp>
#include <vultra/core/base/api_annotations.hpp>
namespace vultra {
struct VULTRA_REFERENCE("node") NodeId { StableId value; };
struct VULTRA_REFLECT CollectionProbe {
    VULTRA_PROPERTY("reference=Node;flags=serialize|bind") std::vector<NodeId> links;
    VULTRA_PROPERTY("flags=serialize|bind") std::vector<uint32_t> counts;
};
enum class NestedMode { eFirst, eSecond };
struct VULTRA_REFLECT NestedSettings {
    VULTRA_PROPERTY("min=0;max=2") float gain = 0.5f;
    VULTRA_PROPERTY("options=First|Second") NestedMode mode = NestedMode::eFirst;
};
struct VULTRA_REFLECT_COMPONENT ComponentProbe {
    VULTRA_PROPERTY("flatten=true;json=/settings") NestedSettings settings{1.25f, NestedMode::eSecond};
}; }''', encoding="utf-8")
            source = root / "collection_probe.cpp"
            source.write_text("#include <vultra/collection_probe.hpp>\n", encoding="utf-8")
            command = dict(original)
            command["file"] = source.as_posix()
            command["arguments"] = [source.as_posix() if item == original["file"] else item
                                    for item in original["arguments"]]
            command["arguments"][1:1] = ["-I" + include.as_posix()]
            types = codegen.parse_reflection(codegen.translation_unit([command], "collection_probe.cpp"))
            self.assertEqual(types[0]["fields"][1]["integer_bits"], 32)
            (include / "vultra/collection_properties.hpp").write_text(codegen.property_header(types), encoding="utf-8")
            source.write_text(codegen.property_source(types, "vultra/collection_properties.hpp"), encoding="utf-8")
            codegen.translation_unit([command], "collection_probe.cpp", skip_bodies=False)

    def test_semantic_accessors_and_inherited_schema(self):
        source = '''namespace vultra {
class [[clang::annotate("vultra.reflect.node")]] Base {
public:
    [[clang::annotate("vultra.property:label=Enabled;set=setEnabled;default=false")]] bool enabled() const;
    void setEnabled(bool value);
};
class [[clang::annotate("vultra.reflect.node")]] Derived : public Base {
public:
    [[clang::annotate("vultra.property:label=Visible;set=setVisible;default=true")]] bool visible() const;
    void setVisible(bool value);
}; }'''
        types = parse(source)
        self.assertEqual([field["name"] for field in types[1]["fields"]], ["enabled", "visible"])
        generated = codegen.property_source(types, "vultra/probe.hpp")
        self.assertIn("static_cast<const Derived*>(object)->enabled()", generated)
        self.assertIn("static_cast<Derived*>(object)->setEnabled", generated)
        self.assertIn("&baseType()", generated)
        with self.assertRaises(RuntimeError):
            parse(source.replace("void setVisible(bool value)", "void setVisible(int value)"))
        with self.assertRaises(RuntimeError):
            parse(source.replace("visible()", "enabled()"))

    def test_reference_and_collection_contracts(self):
        source = '''namespace std { template<class T> class vector {}; }
namespace vultra { struct StableId {}; struct [[clang::annotate("vultra.reference:node")]] NodeId { StableId value; };
struct [[clang::annotate("vultra.reflect")]] Links {
    [[clang::annotate("vultra.property:reference=Node;flags=serialize|bind")]] std::vector<NodeId> targets;
}; }'''
        types = parse(source)
        field = types[0]["fields"][0]
        self.assertEqual(field["kind"], "eArray")
        self.assertEqual(field["element_kind"], "eReference")
        generated = codegen.property_source(types, "vultra/probe.hpp")
        self.assertIn("NodeId{std::get<PropertyReference>(element).id}", generated)
        self.assertIn('PropertyKind::eReference, "node", "Node", true', generated)
        with self.assertRaises(RuntimeError):
            parse(source.replace("reference=Node;", ""))
        with self.assertRaises(RuntimeError):
            parse(source.replace("reference=Node;", "reference=Node;nullable=maybe;"))

    def test_macro_annotated_defaults_reach_safe_managed_values(self):
        source = '''#define PROPERTY [[clang::annotate("vultra.property:label=Gain;min=-2;max=2")]]
namespace vultra {
struct [[clang::annotate("vultra.reflect")]] Probe {
    PROPERTY float value = -0.5f;
}; }'''
        types = parse(source)
        self.assertEqual(types[0]["fields"][0]["default_cpp"], "-0.5f")
        generated = codegen.csharp_values({"types": types, "pods": [{"name": "Probe"}]})
        self.assertIn("public float Value { get; init; } = -0.5f;", generated)
        self.assertNotIn("unsafe", generated)
        with self.assertRaises(RuntimeError):
            parse(source.replace("-0.5f", "1.0f / 2.0f"))
        with self.assertRaises(RuntimeError):
            parse(source.replace("PROPERTY float value = -0.5f;", "PROPERTY float value; Probe() : value(1) {}"))

    def test_types_flags_paths_and_enum_values(self):
        types = parse(declaration(kind="int", metadata="label=Count;min=0;max=20;flags=serialize|inspect|reload;json=/counts/0"))
        field = types[0]["fields"][0]
        self.assertEqual(field["kind"], "eInt")
        self.assertEqual(field["integer_bits"], 32)
        self.assertEqual(field["flag_bits"], 11)
        self.assertEqual(field["json_path"], "/counts/0")
        source = 'namespace vultra { enum class Mode { eFirst = 3, eSecond = 9, eHidden = 20 }; }'
        types = parse(source + declaration(kind="Mode", metadata="label=Mode;options=First|Second"))
        self.assertEqual(types[0]["fields"][0]["enum_values"], [3, 9, 20])
        self.assertEqual(types[0]["fields"][0]["choices"], [{"label": "First", "value": 3},
                                                         {"label": "Second", "value": 9}])
        output = codegen.property_source(types, "vultra/probe.hpp")
        self.assertIn("static_cast<Mode>", output)
        self.assertIn("static_cast<const Probe*>(object)->value", output)

    def test_multiple_types_and_explicit_fields(self):
        types = parse(declaration("First") + declaration("Second"))
        self.assertEqual([item["name"] for item in types], ["First", "Second"])
        for item in types:
            self.assertEqual([field["name"] for field in item["fields"]], ["value"])
            self.assertEqual(item["fields"][0]["label"], "Gain")

    def test_unsupported_fields_and_metadata(self):
        cases = [
            ("int", "label=Count"),
            ("float", "label=Gain"),
            ("float", "min=2;max=1"),
            ("float", "min=0;max=inf"),
            ("float", "min=0;max=2;widget=unknown"),
            ("float", "min=0;max=2;widget=drag"),
            ("float", "min=0;max=2;widget=drag;speed=-1"),
            ("float", "min=bad;max=2"),
            ("float", "min=0;max=2;minimum=1"),
            ("float", "min=0;max=2;min=1"),
            ("float", "min=0;max=2;flags=serialize|unknown"),
            ("float", "min=0;max=2;flags=serialize|serialize"),
            ("float", "min=0;max=2;json=value"),
            ("float", "min=0;max=2;json=/wrong~escape"),
            ("float", "min=0;max=2;options=First"),
            ("float", "min=0;max=1e99"),
            ("float", "min=0;max=2;widget=drag;speed=1e99"),
            ("int", "min=0;max=0.5"),
            ("int", "min=0;max=4294967295"),
            ("int64_t", "min=0;max=9223372036854775807"),
            ("bool", "label=Enabled;widget=drag;speed=1"),
        ]
        for kind, metadata in cases:
            with self.subTest(kind=kind, metadata=metadata), self.assertRaises(RuntimeError):
                parse(declaration(kind=kind, metadata=metadata))

    def test_colliding_json_paths_are_rejected(self):
        for second in ("/settings", "/settings/gain"):
            source = '''namespace vultra {
struct [[clang::annotate("vultra.reflect")]] Probe {
    [[clang::annotate("vultra.property:label=First;min=0;max=2;json=/settings")]] float first;
    [[clang::annotate("vultra.property:label=Second;min=0;max=2;json=PATH")]] float second;
}; }'''.replace("PATH", second)
            with self.subTest(path=second), self.assertRaises(RuntimeError):
                parse(source)


def experiment_probe(field="uint64_t count;", method="uint64_t open(uint32_t width);", *, packing=""):
    return f'''using uint32_t = unsigned int;
using uint64_t = unsigned long long;
namespace vultra {{
{packing}
struct [[clang::annotate("vultra.bind.pod")]] Probe {{ {field} }};
class ExperimentHost {{ public:
    [[clang::annotate("vultra.bind.experiment")]] {method}
}};
}}'''


class ExperimentTests(unittest.TestCase):
    def test_fixed_width_pod_and_instance_methods(self):
        pods, methods = codegen.parse_experiments(unit(experiment_probe(), "experiment_host.hpp"))
        self.assertEqual(pods[0]["fields"], [{"name": "count", "type": "uint64_t"}])
        self.assertEqual(methods[0]["name"], "open")
        self.assertEqual(methods[0]["arguments"], [{"name": "width", "type": "uint32_t"}])
        self.assertTrue(methods[0]["host_mutable"])

    def test_scene_pods_are_shared_by_value(self):
        source = experiment_probe(method="void setSettings(uint64_t session, CameraSettings value);")
        source = source.replace("class ExperimentHost", "struct CameraSettings { float verticalFov; };\nclass ExperimentHost")
        camera = {"name": "CameraSettings", "fields": [{"name": "verticalFov", "type": "float"}]}
        pods, methods = codegen.parse_experiments(unit(source, "experiment_host.hpp"), [camera])
        self.assertEqual(next(pod for pod in pods if pod["name"] == "CameraSettings"), camera)
        self.assertEqual(methods[0]["arguments"][1]["type"], "CameraSettings")
        parameters = codegen.experiment_parameters(methods[0], {pod["name"] for pod in pods})
        self.assertIn(("value", "VultraCameraSettings"), parameters)

    def test_python_scene_defaults_use_reflected_values(self):
        camera = {"name": "CameraSettings", "fields": [{"name": "verticalFov", "type": "float"}]}
        source = codegen.python_scene_controls({
            "pods": [camera], "experiment_pods": [camera],
            "types": [{"name": "CameraSettings", "fields": [
                {"name": "verticalFov", "type": "float", "default_cpp": "0.75f"}]}],
            "experiment_functions": [
                {"name": "cameraSettings", "return": "CameraSettings", "arguments": [
                    {"name": "session", "type": "uint64_t"}, {"name": "node", "type": "std::string_view"}]},
                {"name": "setCameraSettings", "return": "void", "arguments": [
                    {"name": "session", "type": "uint64_t"}, {"name": "node", "type": "std::string_view"},
                    {"name": "value", "type": "CameraSettings"}]}],
        })
        self.assertIn("vertical_fov: float = 0.75", source)
        self.assertIn("def camera_settings(self, identifier: str)", source)
        self.assertIn("def set_camera_settings(self, identifier: str, value: CameraSettings)", source)
        self.assertIn("_finite_float(self.vertical_fov)", source)

    def test_unsupported_signatures_and_layouts(self):
        cases = [
            experiment_probe(field="int count;"),
            experiment_probe(field="uint32_t count : 3;"),
            experiment_probe(field="uint32_t count; uint64_t size;", packing="#pragma pack(1)"),
            experiment_probe(method="static uint64_t open(uint32_t width);"),
            experiment_probe(method="uint64_t* open(uint32_t width);"),
            experiment_probe(method="uint64_t open(void* width);"),
        ]
        for source in cases:
            with self.subTest(source=source), self.assertRaises(RuntimeError):
                codegen.parse_experiments(unit(source, "experiment_host.hpp"))


if __name__ == "__main__":
    unittest.main()
