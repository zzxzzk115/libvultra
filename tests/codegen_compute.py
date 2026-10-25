"""Compute domain discovery and signature rejection; the shared generator owns this test."""
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

from clang import cindex
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import codegen

MARKER = '[[clang::annotate("vultra.bind.compute")]]'


def parse(declarations):
    filename = str(codegen.ROOT / "build/.tmp/include/vultra/compute_probe.hpp")
    source = "using uint64_t = unsigned long; namespace vultra { class ComputeContext { public: " + declarations + " }; }"
    unit = cindex.Index.create().parse(filename, args=["-x", "c++", "-std=c++23"], unsaved_files=[(filename, source)])
    errors = [str(d) for d in unit.diagnostics if d.severity >= cindex.Diagnostic.Error]
    if errors:
        raise AssertionError("\n".join(errors))
    with patch("codegen.translation_unit", return_value=unit):
        return codegen.parse_api([], {"compute": "probe.cpp"})


class ComputeDomainTests(unittest.TestCase):
    def test_tagged_methods_generate_c_and_python_from_one_ir(self):
        ir = parse(MARKER + " uint64_t createValue(uint64_t count); void internalOnly();")
        self.assertEqual([item["name"] for item in ir["compute_functions"]], ["createValue"])
        self.assertIn("*create_value)", codegen.codegen_compute.header(ir))
        self.assertIn("context->value.createValue(count)", codegen.codegen_compute.source(ir))
        self.assertIn('("create_value", ctypes.CFUNCTYPE', codegen.codegen_compute.python_bindings(ir))

    def test_unsupported_signatures_and_static_methods_fail(self):
        for declaration in ("void pointer(void* data);", "static void staticCall();", "int badReturn();"):
            with self.subTest(declaration=declaration), self.assertRaisesRegex(RuntimeError, "Compute|compute"):
                parse(MARKER + declaration)

    def test_reserved_names_and_lowercase_collisions_fail(self):
        for declaration in ("void createContext();", "void sampleValue(); " + MARKER + " void sample_value();"):
            with self.subTest(declaration=declaration), self.assertRaisesRegex(RuntimeError, "Ambiguous"):
                parse(MARKER + declaration)

    def test_generated_output_arguments_cannot_shadow_inputs(self):
        with self.assertRaisesRegex(RuntimeError, "parameter names"):
            parse(MARKER + " uint64_t read(uint64_t value);")


if __name__ == "__main__":
    unittest.main()
