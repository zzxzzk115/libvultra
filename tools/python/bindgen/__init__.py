"""bindgen: the two-stage, IR-based script-binding pipeline.

Stage 1 (extract_bindings.py + bindgen.clang_frontend) parses VBIND_*-annotated
C++ headers with libclang and writes a language-neutral IR
(tools/bindings/ir/bindings.ir.json). Stage 2 backends (bindgen.backends.*)
consume that IR and emit per-language bindings -- sol2 registration + the LuaLS
stub today, Python / C# later -- without re-running the C++ extraction.

The IR is the single source of truth; both stages and every backend load it
through bindgen.ir_loader so the contract lives in one place.
"""

SCHEMA_VERSION = 1
