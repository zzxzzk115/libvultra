"""Repository ownership checks for the shared binding generator (Python/libclang required)."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import subprocess

from clang import cindex

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import codegen


def ui_unit(declarations, header="project_widgets.hpp"):
    filename = str(codegen.ROOT / "build/.tmp/include/project" / header)
    source = '''using int32_t = int;
namespace std { struct string_view {}; }
namespace vultra { class EditorGuiFrame;
''' + declarations + "\n}"
    unit = cindex.Index.create().parse(
        filename, args=["-x", "c++", "-std=c++23"], unsaved_files=[(filename, source)])
    failures = [str(item) for item in unit.diagnostics if item.severity >= cindex.Diagnostic.Error]
    if failures:
        raise AssertionError("\n".join(failures))
    return unit


def parse_ui(declarations, header="project_widgets.hpp"):
    with patch("codegen.translation_unit", return_value=ui_unit(declarations, header)):
        return codegen.parse_api([], {"ui": "project.cpp"})


UI_MARKER = '[[clang::annotate("vultra.bind.ui")]]'


class DomainTests(unittest.TestCase):
    def test_same_size_signature_changes_have_different_fingerprints(self):
        first = parse_ui(UI_MARKER + " bool guiPreview(EditorGuiFrame& frame, int32_t count);")
        second = parse_ui(UI_MARKER + " bool guiPreview(EditorGuiFrame& frame, double count);")
        self.assertNotEqual(codegen.fingerprint(first, "ui"), codegen.fingerprint(second, "ui"))
        unrelated = dict(first, scene_functions=[{"name": "unrelated", "arguments": [], "return": "void"}])
        self.assertEqual(codegen.fingerprint(first, "ui"), codegen.fingerprint(unrelated, "ui"))
        self.assertIn("uint64_t fingerprint;", codegen.c_header(first))
        self.assertNotIn("__FINGERPRINT__", codegen.c_header(first))

    def test_compile_command_labels_do_not_select_binding_domains(self):
        unit = ui_unit(UI_MARKER + " bool guiPreview(EditorGuiFrame& frame, std::string_view label);")
        with patch("codegen.translation_unit", return_value=unit):
            ir = codegen.parse_api([], {"project_tools": "project.cpp", "second_include": "other.cpp"})
        self.assertEqual(len(ir["ui_functions"]), 1)
        signature = ir["ui_functions"][0]["signature"]
        self.assertEqual(signature["receiver_lifetime"], "callback")
        self.assertEqual(signature["parameters"][0]["kind"], "utf8")
        self.assertEqual(signature["parameters"][0]["lifetime"], "call")
        self.assertIn("*preview)", codegen.c_header(ir))

    def test_tagged_functions_follow_their_public_headers(self):
        declaration = UI_MARKER + " bool guiPreview(EditorGuiFrame& frame, std::string_view label);"
        first = parse_ui(declaration)
        moved = parse_ui(declaration, "tools/preview.hpp")
        self.assertEqual(first["ui_functions"][0]["qualified_name"], "vultra::guiPreview")
        self.assertEqual(moved["ui_functions"][0]["header"], "project/tools/preview.hpp")
        self.assertEqual(codegen.c_header(first), codegen.c_header(moved))
        self.assertIn("#include <project/tools/preview.hpp>", codegen.c_source(moved))
        self.assertIn("vultra::guiPreview(current,", codegen.c_source(moved))

    def test_unannotated_functions_stay_out_of_the_abi(self):
        ir = parse_ui(UI_MARKER + " void guiHelp(EditorGuiFrame& frame, std::string_view text);\n"
                      "void internalOnly(void* implementation);")
        self.assertEqual([function["name"] for function in ir["ui_functions"]], ["guiHelp"])

    def test_repeated_declarations_are_one_export(self):
        declaration = UI_MARKER + " bool guiPreview(EditorGuiFrame& frame, std::string_view label);\n"
        ir = parse_ui(declaration + "bool guiPreview(EditorGuiFrame& frame, std::string_view label) { return false; }")
        self.assertEqual(len(ir["ui_functions"]), 1)
        unit = ui_unit(declaration)
        self.assertEqual(len(codegen.bound_declarations(
            [unit, unit], "vultra.bind.ui", cindex.CursorKind.FUNCTION_DECL)), 1)

    def test_new_signatures_need_no_emitter_edit(self):
        ir = parse_ui(UI_MARKER + " bool projectToggle(EditorGuiFrame& frame, std::string_view label);")
        self.assertIn("*projecttoggle)", codegen.c_header(ir))
        self.assertIn("vultra::projectToggle(current,", codegen.c_source(ir))
        self.assertIn(" ProjectToggle;", codegen.csharp_bindings(ir))

    def test_frame_only_calls_generate_valid_c_and_managed_signatures(self):
        ir = parse_ui(UI_MARKER + " void guiFinish(EditorGuiFrame& frame);")
        self.assertIn("*finish)(VultraUiFrame frame)", codegen.c_header(ir))
        self.assertIn("vultra::guiFinish(current)", codegen.c_source(ir))
        self.assertIn("delegate* unmanaged[Cdecl]<VultraUiFrame, VultraStatus> Finish;", codegen.csharp_bindings(ir))

    def test_generated_parameter_names_cannot_shadow_arguments(self):
        for declaration in [
            UI_MARKER + " bool guiValue(EditorGuiFrame& frame, double changed);",
            UI_MARKER + " void guiLabel(EditorGuiFrame& frame, std::string_view label, double labelSize);",
        ]:
            with self.subTest(declaration=declaration), self.assertRaisesRegex(RuntimeError, "parameter names"):
                parse_ui(declaration)

    def test_unsupported_annotations_fail_instead_of_disappearing(self):
        invalid = [
            '[[clang::annotate("vultra.bind.uix")]] void typo();',
            UI_MARKER + " void guiVariadic(EditorGuiFrame& frame, ...);",
            UI_MARKER + " void guiUnnamed(EditorGuiFrame&, std::string_view);",
            "struct " + UI_MARKER + " WrongKind {};",
            UI_MARKER + ' [[clang::annotate("vultra.bind.scene")]] void wrongDomain();',
            UI_MARKER + " int* guiPointer(EditorGuiFrame& frame, std::string_view label);",
        ]
        for declaration in invalid:
            with self.subTest(declaration=declaration), self.assertRaises(RuntimeError):
                parse_ui(declaration)

    def test_ambiguous_c_exports_fail(self):
        for declaration in [
            UI_MARKER + " bool guiPreview(EditorGuiFrame& frame, std::string_view label);\n" +
            UI_MARKER + " void guiPreview(EditorGuiFrame& frame, std::string_view label, double value);",
            UI_MARKER + " void guiHelp(EditorGuiFrame& frame, std::string_view label);\n" +
            UI_MARKER + " void guihelp(EditorGuiFrame& frame, std::string_view label);",
        ]:
            with self.subTest(declaration=declaration), self.assertRaisesRegex(RuntimeError, "Ambiguous"):
                parse_ui(declaration)

    def test_reflected_names_cannot_silently_replace_another_type(self):
        source = '''namespace first {
struct [[clang::annotate("vultra.reflect")]] Settings {
    [[clang::annotate("vultra.property:min=0;max=4")]] float gain = 1;
}; }
namespace second {
struct [[clang::annotate("vultra.reflect")]] Settings {
    [[clang::annotate("vultra.property:min=0;max=4")]] float gain = 1;
}; }'''
        with self.assertRaisesRegex(RuntimeError, "Ambiguous reflected type name"):
            codegen.parse_reflection(ui_unit(source))

    def test_format_failure_keeps_all_outputs_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.h"
            second = root / "second.h"
            first.write_text("original\n")
            files = {first: "new\n", second: "new\n"}
            with patch("codegen.subprocess.run", side_effect=[
                    subprocess.CompletedProcess([], 0, stdout="formatted\n"),
                    subprocess.CalledProcessError(1, "clang-format")]):
                with self.assertRaises(subprocess.CalledProcessError):
                    codegen.update_outputs(files, check=False)
            self.assertEqual(first.read_text(), "original\n")
            self.assertFalse(second.exists())

    def test_output_validation_precedes_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ir = {"types": []}
            specifications = [{"path": "api.json", "emitter": "ir"},
                              {"path": "properties.hpp", "emitter": "property_header",
                               "declarations": ["project/missing.hpp"]}]
            with self.assertRaisesRegex(ValueError, "Missing reflected declarations"):
                codegen.generate_outputs(ir, specifications, root)
            self.assertEqual(list(root.iterdir()), [])

    def test_new_marked_types_join_their_owner_without_a_type_list(self):
        types = [{"name": "First", "header": "project/widgets.hpp", "category": "eValue"},
                 {"name": "Other", "header": "other/widgets.hpp", "category": "eValue"},
                 {"name": "SceneNode", "header": "project/nodes.hpp", "category": "eNode"}]
        specification = [{"path": "properties.hpp", "emitter": "property_header",
                          "declarations": ["project/*.hpp"], "categories": ["eValue"]}]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = next(iter(codegen.generate_outputs({"types": types}, specification, root).values()))
            types.append({"name": "Added", "header": "project/widgets.hpp", "category": "eValue"})
            after = next(iter(codegen.generate_outputs({"types": types}, specification, root).values()))
        self.assertIn("firstType()", before)
        self.assertNotIn("addedType()", before)
        self.assertIn("addedType()", after)
        self.assertNotIn("otherType()", after)
        self.assertNotIn("sceneNodeType()", after)

    def test_missing_callback_declarations_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "Missing callback table"):
                codegen.generate_outputs({"callback_tables": []}, [
                    {"path": "callback.h", "emitter": "callback_header", "tables": ["Missing"]}], Path(directory))

    def test_outputs_stay_inside_their_owner(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for specifications in ([{"path": "../other/api.json", "emitter": "ir"}],
                                   [{"path": "api.json", "emitter": "ir"}] * 2):
                with self.assertRaisesRegex(ValueError, "unique and inside"):
                    codegen.generate_outputs({}, specifications, root)

    def test_unknown_emitter_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "Unknown code generator"):
                codegen.generate_outputs({}, [{"path": "api.h", "emitter": "typo"}], Path(directory))

    def test_ir_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            specification = [{"path": "api.json", "emitter": "ir"}]
            first = codegen.generate_outputs({"version": 1, "types": []}, specification, root)
            second = codegen.generate_outputs({"types": [], "version": 1}, specification, root)
            self.assertEqual(first, second)
            self.assertEqual(json.loads(next(iter(first.values())))["version"], 1)


if __name__ == "__main__":
    unittest.main()
