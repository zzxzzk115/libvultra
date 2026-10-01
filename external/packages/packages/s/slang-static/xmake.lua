package("slang-static")
    set_homepage("https://github.com/shader-slang/slang")
    set_description("Slang shader compiler, linked into Vultra executables")
    set_license("MIT")
    add_urls("https://github.com/shader-slang/slang.git")
    add_versions("2026.11", "03f76f6c91fd4c6db1c067ee92ad1af188d302b6")
    add_deps("cmake", "miniz")
    if is_plat("linux") then
        add_syslinks("dl", "pthread")
    end

    on_load(function (package)
        package:add("defines", "SLANG_STATIC")
        package:add("links", "slang-compiler", "compiler-core", "core", "lz4", "cmark-gfm")
    end)

    on_install("windows|x64", "linux|x86_64", function (package)
        io.replace("CMakeLists.txt", [[find_package(Threads REQUIRED)]], [[find_package(Threads REQUIRED)
find_package(miniz CONFIG REQUIRED)
add_library(miniz ALIAS miniz::miniz)
get_target_property(MINIZ_INCLUDE_DIRS miniz::miniz INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${MINIZ_INCLUDE_DIRS})]], {plain = true})
        -- Slang 2026.11 uses INT_MIN without including its defining header under GCC 16.
        io.replace("source/slang/slang-parser.cpp", "#include <optional>",
                   "#include <optional>\n#include <climits>", {plain = true})
        -- The GLSL module is EXCLUDE_FROM_ALL but Slang 2026.11 still installs it.
        -- Direct SPIR-V emission uses the embedded compiler and needs no module sidecar.
        io.replace("source/slang-glsl-module/CMakeLists.txt", "    INSTALL\n)", ")", {plain = true})
        -- This static build omits slang-glslang, so do not register its downstream locators.
        -- Direct SPIR-V emission can proceed without the optional SPIRV-Opt module.
        local locators = "source/compiler-core/slang-downstream-compiler-util.cpp"
        for _, line in ipairs({
            "    outFuncs[int(SLANG_PASS_THROUGH_GLSLANG)] = &GlslangDownstreamCompilerUtil::locateCompilers;\n",
            "    outFuncs[int(SLANG_PASS_THROUGH_SPIRV_OPT)] = &SpirvOptDownstreamCompilerUtil::locateCompilers;\n",
            "    outFuncs[int(SLANG_PASS_THROUGH_SPIRV_DIS)] = &SpirvDisDownstreamCompilerUtil::locateCompilers;\n"
        }) do
            assert(io.readfile(locators):find(line, 1, true), "Slang downstream locator patch is outdated")
            io.replace(locators, line, "", {plain = true})
        end
        local configs = {
            "-DSLANG_LIB_TYPE=STATIC",
            "-DSLANG_ENABLE_RELEASE_DEBUG_INFO=OFF",
            "-DSLANG_ENABLE_TESTS=OFF",
            "-DSLANG_RHI_BUILD_TESTS=OFF",
            "-DSLANG_RHI_BUILD_EXAMPLES=OFF",
            "-DSLANG_ENABLE_EXAMPLES=OFF",
            "-DSLANG_ENABLE_GFX=OFF",
            "-DSLANG_ENABLE_SLANG_RHI=OFF",
            "-DSLANG_ENABLE_DXIL=OFF",
            "-DSLANG_ENABLE_SLANGI=OFF",
            "-DSLANG_ENABLE_REPLAYER=OFF",
            "-DSLANG_ENABLE_SLANG_PROXY=OFF",
            "-DSLANG_ENABLE_SLANGD=OFF",
            "-DSLANG_ENABLE_SLANGC=OFF",
            "-DSLANG_ENABLE_SLANGRT=OFF",
            "-DSLANG_ENABLE_SLANG_GLSLANG=OFF",
            "-DSLANG_SLANG_LLVM_FLAVOR=DISABLE",
            "-DSLANG_USE_SYSTEM_MINIZ=ON",
            "-DSLANG_EMBED_STDLIB_SOURCE=ON",
            "-DSLANG_EMBED_STDLIB=ON"
        }
        import("package.tools.cmake").install(package, configs, {jobs = 2})
        local extension = package:is_plat("windows") and ".lib" or ".a"
        local archives = os.files("build_*/**" .. extension)
        for _, name in ipairs({"compiler-core", "core", "lz4", "cmark-gfm"}) do
            local matches = {}
            for _, archive in ipairs(archives) do
                local filename = path.filename(archive)
                if filename == name .. extension or filename == "lib" .. name .. extension then
                    table.insert(matches, archive)
                end
            end
            assert(#matches == 1, "Expected one Slang static archive for " .. name)
            os.cp(matches[1], package:installdir("lib"))
        end
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <slang-com-ptr.h>
            #include <slang.h>
            void test() {
                Slang::ComPtr<slang::IGlobalSession> global;
                slang::createGlobalSession(global.writeRef());
            }
        ]]}, {configs = {languages = "c++17"}}))
    end)
package_end()
