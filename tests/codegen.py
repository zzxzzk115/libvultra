"""Tool-side reflection checks; requires the same Python/libclang setup as xmake codegen."""

from pathlib import Path
import sys
import unittest

from clang import cindex

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import codegen


def declaration(name="Probe", kind="float", metadata="label=Gain;min=0;max=2"):
    return (
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


class ReflectionTests(unittest.TestCase):
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
        ]
        for kind, metadata in cases:
            with self.subTest(kind=kind, metadata=metadata), self.assertRaises(RuntimeError):
                parse(declaration(kind=kind, metadata=metadata))


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
