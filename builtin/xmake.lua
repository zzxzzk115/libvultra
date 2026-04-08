-- -- Shader compile task
-- task("shader_task")
--     on_run(function ()
--         import("core.project.config")
--         import("core.base.option")

--         local projectdir = get_config("project_dir")

--         local shader_root = path.join(projectdir, "builtin/shaders")
--         local spv_root = path.join(projectdir, "builtin/shader_spvs")
-- 		local shader_header_root = path.join(projectdir, "builtin/generated/include/shader_headers")
--         local shader_config_root = path.join(projectdir, "source/include/vultra/function/renderer/shader_config")
--         os.mkdir(spv_root)

--         -- valid shader stages to compile
--         local stages = {".vert", ".frag", ".comp", ".geom", ".tesc", ".tese",
--                         ".mesh", ".task", ".rgen", ".rmiss", ".rahit", ".rchit",
--                         ".rint", ".rcall"}

--         local files = {}
--         for _, ext in ipairs(stages) do
--             local pattern = path.join(shader_root, "**" .. ext)
--             table.join2(files, os.files(pattern))
--         end

--         for _, f in ipairs(files) do
-- 			print(f)
--             local rel = path.relative(f, shader_root)
--             local out_spv = path.join(spv_root, rel .. ".spv")
--             os.mkdir(path.directory(out_spv))

--             -- incremental build: skip if up-to-date
--             if os.exists(out_spv) and os.mtime(out_spv) >= os.mtime(f) then
--                 cprint("${cyan}[OK]${clear}   %s", rel)
--             else
--                 cprint("${green}[BUILD]${clear} %s", rel)
--                 os.execv("glslangValidator", {
--                     "-V",
-- 					"--target-env", "vulkan1.2",
--                     "-I" .. shader_root,
--                     "-I" .. shader_config_root,
--                     "-o", out_spv,
-- 					"-P#extension GL_ARB_shading_language_include : enable", -- https://github.com/KhronosGroup/glslang/issues/1691#issuecomment-2282322200
-- 					"-P#extension GL_GOOGLE_cpp_style_line_directive : require",
-- 					"-P#define DEPTH_ZERO_TO_ONE 1", -- for Vulkan Depth
--                     f
--                 })

--                 -- read spv binary and write to header file
--                 local spv_data = io.readfile(out_spv, {encoding = "binary"})

--                 -- magic number check
--                 if #spv_data < 4 then
--                     raise("spv file too small: %s", out_spv)
--                 end

--                 local b1, b2, b3, b4 = spv_data:byte(1, 4)
--                 local magic = b1 | (b2 << 8) | (b3 << 16) | (b4 << 24)

--                 if magic ~= 0x07230203 then
--                     raise("invalid SPIR-V magic number in %s: got 0x%08X, expected 0x07230203", out_spv, magic)
--                 end

--                 local header_path = path.join(shader_header_root, rel .. ".spv.h")
--                 os.mkdir(path.directory(header_path))

--                 -- base name + extension -> unique symbol
--                 local base = path.basename(rel):gsub("%.", "_")
--                 local ext  = path.extension(rel):sub(2)
--                 local symbol = base .. "_" .. ext -- e.g. pbr_vert

--                 local header_file = io.open(header_path, "w")
--                 header_file:write("// Auto-generated from " .. rel .. "\n")
--                 header_file:write("#pragma once\n\n")
--                 header_file:write("#include <cstdint>\n")
--                 header_file:write("#include <vector>\n\n")
--                 header_file:write("const std::vector<uint32_t> " .. symbol .. "_spv = {\n    ")

--                 for i = 1, #spv_data, 4 do
--                     if i > 1 then header_file:write(",\n    ") end
--                     local w1, w2, w3, w4 = spv_data:byte(i, i + 3)
--                     local word = w1 | (w2 << 8) | (w3 << 16) | (w4 << 24)
--                     header_file:write(string.format("0x%08X", word))
--                 end

--                 header_file:write("\n};\n")
--                 header_file:close()
--             end
-- 		end
-- 	end)
-- task_end()

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

        local pkg = target:pkg("vshadersystem")
        if not pkg then
            return
        end

        local vshaderc =
            path.join(pkg:installdir(), "bin", "vshaderc")

        local projectdir = get_config("project_dir")

        local shader_root_common =
            path.join(projectdir, "builtin/shaders")
        local build_script =
            path.join(projectdir, "builtin/xmake.lua")

        local lib_root =
            path.join(projectdir, "builtin/shader_lib")

        local header_root =
            path.join(projectdir, "builtin/generated/include")

        local keywords_common =
            path.join(shader_root_common, "builtin_keywords.vkw")

        local vshlib_highend =
            path.join(lib_root, "builtin_highend.vshlib")

        local vshlib_compatibility =
            path.join(lib_root, "builtin_compatibility.vshlib")

        local vshweblib_compatibility =
            path.join(lib_root, "builtin_compatibility.vshweblib")

        local header =
            path.join(header_root, "builtin_shaders.hpp")

        os.mkdir(lib_root)
        os.mkdir(header_root)

        local highend_shader_patterns = {
            "passes/highend/**.vshader",
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

        local function build_vulkan_library(label, shader_root, shader_patterns, keywords_file, output_file)
            local shader_files = collect_shader_files(shader_root, shader_patterns)
            local argv = {
                "build",
                "--shader_root", shader_root,
                "-I", shader_root,
            }
            for _, file in ipairs(shader_files) do
                table.insert(argv, "--shader")
                table.insert(argv, path.relative(file, shader_root))
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
            local processed_root = path.join(os.tmpdir(), "libvultra_webgpu_shader_root")
            os.rm(processed_root)
            os.mkdir(processed_root)
            os.cp(path.join(shader_root, "include"), path.join(processed_root, "include"))

            local shader_files = collect_shader_files(shader_root, shader_patterns)
            local processed_shader_files = {}
            for _, file in ipairs(shader_files) do
                local rel = path.relative(file, shader_root)
                local dst = path.join(processed_root, rel)
                os.mkdir(path.directory(dst))

                local content = io.readfile(file)
                content = content:gsub(
                    "layout%s*%(%s*push_constant%s*%)%s*uniform",
                    "layout(set = 1, binding = 31, std140) uniform")
                io.writefile(dst, content)
                table.insert(processed_shader_files, dst)
            end

            local argv = {
                "build",
                "--webgpu",
                "--material-mode", "ubo",
                "--shader_root", processed_root,
                "-I", processed_root,
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
                                 keywords_common,
                                 vshlib_highend)
        end
        mark_result("builtin_highend.vshlib", rebuild_highend)

        if rebuild_compatibility then
            build_vulkan_library("builtin_compatibility.vshlib",
                                 shader_root_common,
                                 compatibility_shader_patterns,
                                 keywords_common,
                                 vshlib_compatibility)
        end
        mark_result("builtin_compatibility.vshlib", rebuild_compatibility)

        if rebuild_webgpu_compatibility then
            local ok = build_webgpu_library("builtin_compatibility.vshweblib",
                                            shader_root_common,
                                            webgpu_compatibility_shader_patterns,
                                            keywords_common,
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

        ------------------------------------------------
        -- embed header
        ------------------------------------------------

        local rebuild_header = true

        local newest_lib_mtime =
            math.max(os.mtime(vshlib_highend), os.mtime(vshlib_compatibility), os.mtime(vshweblib_compatibility))
        if os.exists(header)
        and os.mtime(header) >= newest_lib_mtime
        then
            rebuild_header = false
        end

        if rebuild_header then
            local data_vshlib_highend =
                io.readfile(vshlib_highend, {encoding="binary"})
            local data_vshlib_compatibility =
                io.readfile(vshlib_compatibility, {encoding="binary"})
            local data_vshweblib_compatibility =
                io.readfile(vshweblib_compatibility, {encoding="binary"})

            local f =
                io.open(header, "w")

            f:write("// auto-generated\n")
            f:write("#pragma once\n\n")
            f:write("#include <cstdint>\n\n")

            local function write_embedded_blob(symbol_name, data)
                f:write(string.format("inline constexpr uint8_t %s[] = {\n", symbol_name))
                for i = 1, #data do
                    if i % 12 == 1 then
                        f:write("    ")
                    end

                    f:write(string.format("0x%02X", data:byte(i)))

                    if i < #data then
                        f:write(",")
                    end

                    if i % 12 == 0 then
                        f:write("\n")
                    end
                end
                f:write("\n};\n\n")
                f:write(string.format("inline constexpr size_t %s_size = %d;\n\n", symbol_name, #data))
            end

            write_embedded_blob("builtin_shaders_highend_vshlib", data_vshlib_highend)
            write_embedded_blob("builtin_shaders_compatibility_vshlib", data_vshlib_compatibility)
            write_embedded_blob("builtin_shaders_compatibility_web_vshweblib", data_vshweblib_compatibility)

            f:close()
        end
        mark_result("builtin_shaders.hpp", rebuild_header)
        emit_summary("builtin shaders", shader_built, shader_skipped, shader_samples)
    end)
task_end()

-- Texture convert task
task("texture_task")
    on_run(function ()
        import("core.project.config")
        import("core.base.option")

        local function emit_summary(label, converted, skipped, samples)
            if converted == 0 then
                cprint("${cyan}[OK]${clear} %s up to date (%d items)", label, skipped)
                return
            end

            cprint("${green}[CONVERT]${clear} %s updated (%d changed, %d unchanged)", label, converted, skipped)
            for _, sample in ipairs(samples) do
                cprint("  - %s", sample)
            end
            if converted > #samples then
                cprint("  - ... and %d more", converted - #samples)
            end
        end

        local projectdir = get_config("project_dir")

        local texture_root = path.join(projectdir, "builtin/textures")
		local texture_header_root = path.join(projectdir, "builtin/generated/include/texture_headers")
        os.mkdir(texture_header_root)

        -- valid texture formats to convert
        local exts = {"png", "jpg", "jpeg", "bmp", "tga", "psd", "gif", "hdr", "pic", "exr", "ktx", "ktx2", "dds", "svg"}

        local files = {}
        for _, ext in ipairs(exts) do
            local pattern = path.join(texture_root, "**" .. ext)
            table.join2(files, os.files(pattern))
        end

        local converted = 0
        local skipped = 0
        local samples = {}

        for _, f in ipairs(files) do
            local rel = path.relative(f, texture_root)
            -- read texture binary and write to header file
            local header_path = path.join(texture_header_root, rel .. ".bintex.h")
            if os.exists(header_path) and os.mtime(header_path) >= os.mtime(f) then
                skipped = skipped + 1
            else
                converted = converted + 1
                if #samples < 6 then
                    table.insert(samples, rel)
                end
                local tex_data = io.readfile(f, {encoding = "binary"})
                os.mkdir(path.directory(header_path))
                -- base name + extension -> unique symbol
                local base = path.basename(rel):gsub("%.", "_")
                local ext  = path.extension(rel):sub(2)
                local symbol = base .. "_" .. ext -- e.g. ltc1_dds

                local header_file = io.open(header_path, "w")
                header_file:write("// Auto-generated from " .. rel .. "\n")
                header_file:write("#pragma once\n\n")
                header_file:write("#include <cstdint>\n")
                header_file:write("#include <string>\n")
                header_file:write("#include <vector>\n\n")
                header_file:write("const std::string " .. symbol .. "_ext = " .. "\"." .. ext .. "\";\n")
                header_file:write("const std::vector<uint8_t> " .. symbol .. "_bintex = {\n    ")

                for i = 1, #tex_data do
                    if i > 1 then header_file:write(",\n    ") end
                    local byte = tex_data:byte(i)
                    header_file:write(string.format("0x%02X", byte))
                end

                header_file:write("\n};\n")
                header_file:close()
            end
		end
        emit_summary("builtin textures", converted, skipped, samples)
	end)
task_end()


task("font_task")
    on_run(function ()
        import("core.project.config")
        import("core.base.option")

        local function emit_summary(label, converted, skipped, samples)
            if converted == 0 then
                cprint("${cyan}[OK]${clear} %s up to date (%d items)", label, skipped)
                return
            end

            cprint("${green}[CONVERT]${clear} %s updated (%d changed, %d unchanged)", label, converted, skipped)
            for _, sample in ipairs(samples) do
                cprint("  - %s", sample)
            end
            if converted > #samples then
                cprint("  - ... and %d more", converted - #samples)
            end
        end

        local projectdir = get_config("project_dir")

        local font_root = path.join(projectdir, "builtin/fonts")
        local font_header_root = path.join(projectdir, "builtin/generated/include/font_headers")
        os.mkdir(font_header_root)

        -- valid font formats
        local exts = {"ttf", "otf"}
        local files = {}

        for _, ext in ipairs(exts) do
            local pattern = path.join(font_root, "**." .. ext)
            table.join2(files, os.files(pattern))
        end

        local converted = 0
        local skipped = 0
        local samples = {}

        for _, f in ipairs(files) do
            local rel = path.relative(f, font_root)
            local header_path = path.join(font_header_root, rel .. ".binfont.h")

            -- check timestamp
            if os.exists(header_path) and os.mtime(header_path) >= os.mtime(f) then
                skipped = skipped + 1
            else
                converted = converted + 1
                if #samples < 4 then
                    table.insert(samples, rel)
                end
                os.mkdir(path.directory(header_path))

                local font_data = io.readfile(f, {encoding = "binary"})
                local base = path.basename(rel):gsub("%.", "_")
                local ext  = path.extension(rel):sub(2)
                local symbol = base .. "_" .. ext  -- e.g. roboto_ttf

                local header_file = io.open(header_path, "w")
                header_file:write("// Auto-generated from " .. rel .. "\n")
                header_file:write("#pragma once\n\n")
                header_file:write("#include <cstddef>\n")
                header_file:write("#include <cstdint>\n\n")

                header_file:write("inline constexpr unsigned char " .. symbol .. "_data[] = {\n")

                for i = 1, #font_data do
                    if (i - 1) % 12 == 0 then header_file:write("    ") end
                    header_file:write(string.format("0x%02X", font_data:byte(i)))
                    if i < #font_data then header_file:write(",") end
                    if i % 12 == 0 then header_file:write("\n") end
                end

                header_file:write("\n};\n")
                header_file:write("inline constexpr size_t " .. symbol .. "_size = sizeof(" .. symbol .. "_data);\n")

                header_file:close()
            end
        end
        emit_summary("builtin fonts", converted, skipped, samples)
    end)
task_end()

if is_plat("android") then
    add_requires("vshadersystem v0.6.2", { configs = { debug = is_mode("debug") }})
else
    add_requires("vshadersystem v0.8.0", { configs = { debug = is_mode("debug") }})
end

target("vultra_builtin_assets")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")

	add_headerfiles("generated/include/**.h")
    add_includedirs("generated/include", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

    add_packages("vshadersystem", {public = true})

    on_config(function (target)
        import("core.base.task")
        task.run("shader_task")
        task.run("texture_task")
        task.run("font_task")
    end)
