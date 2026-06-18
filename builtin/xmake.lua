task("shader_task")
    on_run(function ()
        import("core.project.config")
        import("core.project.project")

        local function emit_summary(label, built, skipped, samples)
            if built == 0 then
                cprint("${cyan}[OK]${clear} %s up to date (%d items)", label, skipped)
                return
            end

            cprint("${green}[BUILD]${clear} %s updated (%d changed, %d unchanged)", label, built, skipped)
            for _, sample in ipairs(samples) do
                cprint("  - %s", sample)
            end
            if built > #samples then
                cprint("  - ... and %d more", built - #samples)
            end
        end

        local target = project.target("vultra_builtin_assets")
        if not target then
            return
        end

        local pkg = project.required_package("vshadersystem~host") or target:pkg("vshadersystem")
        if not pkg then
            return
        end

        local vshaderc = path.join(pkg:installdir(), "bin", "vshaderc")
        if is_host("windows") and not os.isfile(vshaderc) then
            vshaderc = vshaderc .. ".exe"
        end
        if not os.isfile(vshaderc) then
            raise("vshaderc host tool not found: %s", vshaderc)
        end

        local projectdir = get_config("project_dir")

        local shader_root_common =
            path.join(projectdir, "builtin/shaders")
        local build_script =
            path.join(projectdir, "builtin/xmake.lua")

        local lib_root =
            path.join(projectdir, "builtin/shader_lib")

        local keywords_common =
            path.join(shader_root_common, "builtin_keywords.vkw")
        local generated_keywords_root =
            path.join(os.tmpdir(), "libvultra_shader_keywords")
        local keywords_vulkan =
            path.join(generated_keywords_root, "builtin_keywords_vulkan.vkw")
        local keywords_webgpu =
            path.join(generated_keywords_root, "builtin_keywords_webgpu.vkw")

        local vshlib_highend =
            path.join(lib_root, "builtin_highend.vshlib")

        local vshlib_compatibility =
            path.join(lib_root, "builtin_compatibility.vshlib")

        local vshweblib_compatibility =
            path.join(lib_root, "builtin_compatibility.vshweblib")

        os.mkdir(lib_root)

        local highend_shader_patterns = {
            "passes/highend/**.vshader",
            "passes/compatibility/basecolor_cpu.vshader",
            "passes/general/**.vshader",
            "passes/common/**.vshader",
            "passes/shared/**.vshader",
        }
        local compatibility_shader_patterns = {
            "passes/compatibility/**.vshader",
            "passes/general/**.vshader",
            "passes/common/**.vshader",
            "passes/shared/**.vshader",
        }
        local webgpu_compatibility_shader_patterns = {
            "passes/compatibility/**.vshader",
            "passes/general/**.vshader",
        }

        ------------------------------------------------
        -- check rebuild
        ------------------------------------------------

        local function collect_shader_files(shader_root, patterns)
            local files = {}
            for _, pattern in ipairs(patterns) do
                table.join2(files, os.files(path.join(shader_root, pattern)))
            end
            table.sort(files)
            return files
        end

        local function collect_input_files(shader_root, shader_patterns, keywords_file)
            local files = collect_shader_files(shader_root, shader_patterns)
            table.join2(files, os.files(path.join(shader_root, "include/**.glsl")))
            if keywords_file and os.exists(keywords_file) then
                table.insert(files, keywords_file)
            end
            if os.exists(build_script) then
                table.insert(files, build_script)
            end
            if os.exists(vshaderc) then
                table.insert(files, vshaderc)
            end
            return files
        end

        local function needs_rebuild(output_file, input_files)
            if not os.exists(output_file) then
                return true
            end
            local out_mtime = os.mtime(output_file)
            for _, file in ipairs(input_files) do
                if os.exists(file) and os.mtime(file) > out_mtime then
                    return true
                end
            end
            return false
        end

        local rebuild_highend =
            needs_rebuild(vshlib_highend,
                          collect_input_files(shader_root_common, highend_shader_patterns, keywords_common))
        local rebuild_compatibility =
            needs_rebuild(vshlib_compatibility, collect_input_files(shader_root_common, compatibility_shader_patterns, keywords_common))
        local rebuild_webgpu_compatibility =
            needs_rebuild(vshweblib_compatibility, collect_input_files(shader_root_common, webgpu_compatibility_shader_patterns, keywords_common))

        local shader_built = 0
        local shader_skipped = 0
        local shader_samples = {}

        local function mark_result(name, rebuilt)
            if rebuilt then
                shader_built = shader_built + 1
                if #shader_samples < 4 then
                    table.insert(shader_samples, name)
                end
            else
                shader_skipped = shader_skipped + 1
            end
        end

        ------------------------------------------------
        -- build
        ------------------------------------------------

        local function write_platform_keywords(base_keywords_file, platform_webgpu, output_file)
            local content = io.readfile(base_keywords_file)
            if not content then
                raise("failed to read builtin keywords file: %s", base_keywords_file)
            end

            content = content:gsub("%s*$", "")
            content = content .. "\nset PLATFORM_WEBGPU=" .. (platform_webgpu and "1" or "0") .. "\n"

            os.mkdir(path.directory(output_file))
            io.writefile(output_file, content)
        end

        write_platform_keywords(keywords_common, false, keywords_vulkan)
        write_platform_keywords(keywords_common, true, keywords_webgpu)

        local function inject_platform_define(content, platform_webgpu)
            local define_line = "#define PLATFORM_WEBGPU " .. (platform_webgpu and "1" or "0") .. "\n"
            local stage_sections = {
                "vert",
                "vertex",
                "frag",
                "fragment",
                "comp",
                "compute",
                "mesh",
                "task",
                "rgen",
                "raygen",
                "rmiss",
                "miss",
                "raymiss",
                "rchit",
                "closesthit",
                "raychit",
                "rahit",
                "anyhit",
                "rayahit",
                "rint",
                "intersect",
                "rayint",
            }

            for _, section in ipairs(stage_sections) do
                content = content:gsub("(%[" .. section .. "%]%s*\n)", "%1" .. define_line)
            end

            return content
        end

        -- Build libraries from a processed shader root so backend-specific macros are baked into the sources.
        local function prepare_shader_root(temp_dir_name, shader_root, shader_patterns, platform_webgpu, rewrite_push_constants)
            local processed_root = path.join(os.tmpdir(), temp_dir_name)
            os.rm(processed_root)
            os.mkdir(processed_root)
            os.cp(path.join(shader_root, "include"), path.join(processed_root, "include"))

            local shader_files = collect_shader_files(shader_root, shader_patterns)
            for _, file in ipairs(shader_files) do
                local rel = path.relative(file, shader_root)
                local dst = path.join(processed_root, rel)
                os.mkdir(path.directory(dst))

                local content = io.readfile(file)
                content = inject_platform_define(content, platform_webgpu)
                if rewrite_push_constants then
                    content = content:gsub(
                        "layout%s*%(%s*push_constant%s*%)%s*uniform",
                        "layout(set = 1, binding = 31, std140) uniform")
                end

                io.writefile(dst, content)
            end

            return processed_root, collect_shader_files(processed_root, shader_patterns)
        end

        -- Pack the builtin GLSL include tree into a .vshglsl library and mount it
        -- at the VFS root, so shaders resolve `#include "include/..."` by absolute
        -- VFS path regardless of their own directory (the clean, unambiguous path).
        local function pack_include_library(processed_root)
            local inc_lib = processed_root .. ".includes.vshglsl"
            os.execv(vshaderc, {"pack-glsl", "--root", processed_root, "-o", inc_lib})
            return inc_lib
        end

        local function build_vulkan_library(label, shader_root, shader_patterns, keywords_file, output_file)
            local processed_root, processed_shader_files =
                prepare_shader_root("libvultra_vulkan_shader_root", shader_root, shader_patterns, false, false)

            local inc_lib = pack_include_library(processed_root)
            local argv = {
                "build",
                "--shader_root", processed_root,
                "--mount", "=" .. inc_lib,
            }
            for _, file in ipairs(processed_shader_files) do
                table.insert(argv, "--shader")
                table.insert(argv, path.relative(file, processed_root))
            end
            if keywords_file and os.exists(keywords_file) then
                table.insert(argv, "--keywords-file")
                table.insert(argv, keywords_file)
            end
            table.insert(argv, "-o")
            table.insert(argv, output_file)
            os.execv(vshaderc, argv)
        end

        local function build_webgpu_library(label, shader_root, shader_patterns, keywords_file, output_file)
            local processed_root, processed_shader_files =
                prepare_shader_root("libvultra_webgpu_shader_root", shader_root, shader_patterns, true, true)

            local inc_lib = pack_include_library(processed_root)
            local argv = {
                "build",
                "--webgpu",
                "--material-mode", "ubo",
                "--shader_root", processed_root,
                "--mount", "=" .. inc_lib,
            }
            for _, file in ipairs(processed_shader_files) do
                table.insert(argv, "--shader")
                table.insert(argv, path.relative(file, processed_root))
            end
            if keywords_file and os.exists(keywords_file) then
                table.insert(argv, "--keywords-file")
                table.insert(argv, keywords_file)
            end
            table.insert(argv, "-o")
            table.insert(argv, output_file)
            local ok = false
            local err = nil
            try
            {
                function ()
                    os.execv(vshaderc, argv)
                    ok = true
                end,
                catch
                {
                    function (errors)
                        err = errors
                    end
                }
            }
            if not ok then
                cprint("${yellow}[WARN]${clear} failed to build %s: %s", label, tostring(err))
            end
            return ok
        end

        if rebuild_highend then
            build_vulkan_library("builtin_highend.vshlib",
                                 shader_root_common,
                                 highend_shader_patterns,
                                 keywords_vulkan,
                                 vshlib_highend)
        end
        mark_result("builtin_highend.vshlib", rebuild_highend)

        if rebuild_compatibility then
            build_vulkan_library("builtin_compatibility.vshlib",
                                 shader_root_common,
                                 compatibility_shader_patterns,
                                 keywords_vulkan,
                                 vshlib_compatibility)
        end
        mark_result("builtin_compatibility.vshlib", rebuild_compatibility)

        if rebuild_webgpu_compatibility then
            local ok = build_webgpu_library("builtin_compatibility.vshweblib",
                                            shader_root_common,
                                            webgpu_compatibility_shader_patterns,
                                            keywords_webgpu,
                                            vshweblib_compatibility)
            if not ok then
                if os.exists(vshweblib_compatibility) then
                    cprint("${yellow}[WARN]${clear} keep existing builtin_compatibility.vshweblib and continue")
                else
                    cprint("${yellow}[WARN]${clear} builtin_compatibility.vshweblib missing, fallback to builtin_compatibility.vshlib")
                    os.cp(vshlib_compatibility, vshweblib_compatibility)
                end
            end
        end
        mark_result("builtin_compatibility.vshweblib", rebuild_webgpu_compatibility)

        -- When any shader library was rebuilt, force builtin.vpk to re-cook by removing it: the pack
        -- cook's mtime guard (xmake/builtin_pack_cook.lua) can race the freshly-written .vshlib/
        -- .vshweblib and skip the repack, leaving the embedded pack with stale/missing shaders. Deleting
        -- the pack here makes the builtin_pack rule's before_build cook unconditionally on the next build.
        if shader_built > 0 then
            os.tryrm(path.join(os.projectdir(), "builtin", "generated", "builtin.vpk"))
        end

        -- No C-array header is generated anymore. The compiled .vshlib libraries ship in builtin.vpk
        -- (read via builtin::), and the GLSL include sources are read straight from
        -- builtin/shaders/include -- by the engine through the pack, and by the vasset-cli host tool
        -- from disk. Zero generated-header dependencies remain.
        emit_summary("builtin shaders", shader_built, shader_skipped, shader_samples)
    end)
task_end()

local vshadersystem_configs = { debug = is_mode("debug") }
if is_host("windows") then
    vshadersystem_configs.runtimes = is_mode("debug") and "MTd" or "MT"
end

if is_plat("android") then
    add_requires("vshadersystem v0.6.2", { configs = vshadersystem_configs })
    add_requires("vshadersystem~host v0.6.2", { host = true, kind = "binary", configs = vshadersystem_configs })
else
    add_requires("vshadersystem v0.11.1", { configs = vshadersystem_configs })
    add_requires("vshadersystem~host v0.11.1", { host = true, kind = "binary", configs = vshadersystem_configs })
end

target("vultra_builtin_assets")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")

    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

    add_packages("vshadersystem", {public = true})

    on_config(function (target)
        import("core.base.task")
        -- shader_task only compiles the builtin .vshlib libraries (consumed from builtin.vpk). No
        -- C-array headers are generated anymore: everything ships in builtin.vpk straight from its
        -- source files (the builtin_pack rule packs shaders/render graphs/i18n/textures/cursors/
        -- includes directly), so the font/rendergraph/texture/i18n/shader header tasks are retired.
        task.run("shader_task")
    end)
