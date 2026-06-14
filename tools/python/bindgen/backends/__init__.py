"""Stage 2 backends: consume the IR, emit per-language bindings.

lua_sol2     -> source/vultra/src/function/scripting/bindings/script_*_binding.gen.cpp
lua_typestub -> generated sections of tools/lua-stubs/vultra.lua

Future backends (python_pybind, csharp) plug in here, reading the same IR.
"""
