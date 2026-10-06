"""Tool-side reflection checks; requires the same Python/libclang setup as xmake codegen."""

from pathlib import Path
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


class ReflectionTests(unittest.TestCase):
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
