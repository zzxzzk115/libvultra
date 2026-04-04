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
        local shader_root_vulkan_desktop =
            path.join(projectdir, "builtin/shaders/vulkan/desktop")
        local shader_root_vulkan_android =
            path.join(projectdir, "builtin/shaders/vulkan/android")
        local shader_root_webgpu =
            path.join(projectdir, "builtin/shaders/webgpu")

        if not os.exists(shader_root_vulkan_desktop) then
            shader_root_vulkan_desktop = shader_root_common
        end
        if not os.exists(shader_root_vulkan_android) then
            shader_root_vulkan_android = shader_root_common
        end
        if not os.exists(shader_root_webgpu) then
            shader_root_webgpu = shader_root_common
        end

        local lib_root =
            path.join(projectdir, "builtin/shader_lib")

        local header_root =
            path.join(projectdir, "builtin/generated/include")

        local keywords_common =
            path.join(shader_root_common, "builtin_keywords.vkw")
        local keywords_vulkan_desktop =
            path.join(shader_root_vulkan_desktop, "builtin_keywords.vkw")
        local keywords_vulkan_android =
            path.join(shader_root_vulkan_android, "builtin_keywords.vkw")
        local keywords_webgpu =
            path.join(shader_root_webgpu, "builtin_keywords.vkw")

        if not os.exists(keywords_vulkan_desktop) then
            keywords_vulkan_desktop = keywords_common
        end
        if not os.exists(keywords_vulkan_android) then
            keywords_vulkan_android = keywords_common
        end
        if not os.exists(keywords_webgpu) then
            keywords_webgpu = keywords_common
        end

        local vshlib_desktop =
            path.join(lib_root, "builtin_desktop.vshlib")

        local vshlib_android =
            path.join(lib_root, "builtin_android.vshlib")

        local vshweblib_web =
            path.join(lib_root, "builtin_web.vshweblib")

        local header =
            path.join(header_root, "builtin_shaders.hpp")

        local enable_webgpu_shaderlib = not is_plat("android")

        os.mkdir(lib_root)
        os.mkdir(header_root)

        ------------------------------------------------
        -- check rebuild
        ------------------------------------------------

        local function collect_input_files(shader_root, keywords_file)
            local files = {}
            table.join2(files, os.files(path.join(shader_root, "**.vshader")))
            table.join2(files, os.files(path.join(shader_root, "include/**.glsl")))
            if shader_root ~= shader_root_common then
                table.join2(files, os.files(path.join(shader_root_common, "include/**.glsl")))
            end
            if keywords_file and os.exists(keywords_file) then
                table.insert(files, keywords_file)
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

        local rebuild_desktop =
            needs_rebuild(vshlib_desktop, collect_input_files(shader_root_vulkan_desktop, keywords_vulkan_desktop))
        local rebuild_android =
            needs_rebuild(vshlib_android, collect_input_files(shader_root_vulkan_android, keywords_vulkan_android))
        local rebuild_webgpu =
            needs_rebuild(vshweblib_web, collect_input_files(shader_root_webgpu, keywords_webgpu))

        ------------------------------------------------
        -- build
        ------------------------------------------------

        local function build_vulkan_library(label, shader_root, keywords_file, output_file)
            cprint("${green}[BUILD]${clear} " .. label)
            local argv = {
                "build",
                "--shader_root", shader_root,
                "-I", shader_root,
            }
            if shader_root ~= shader_root_common then
                table.insert(argv, "-I")
                table.insert(argv, shader_root_common)
            end
            if keywords_file and os.exists(keywords_file) then
                table.insert(argv, "--keywords-file")
                table.insert(argv, keywords_file)
            end
            table.insert(argv, "-o")
            table.insert(argv, output_file)
            os.execv(vshaderc, argv)
        end

        local function build_webgpu_library(label, shader_root, keywords_file, output_file)
            cprint("${green}[BUILD]${clear} " .. label)
            local argv = {
                "build",
                "--webgpu",
                "--material-mode", "ubo",
                "--shader_root", shader_root,
                "-I", shader_root,
            }
            if shader_root ~= shader_root_common then
                table.insert(argv, "-I")
                table.insert(argv, shader_root_common)
            end
            if keywords_file and os.exists(keywords_file) then
                table.insert(argv, "--keywords-file")
                table.insert(argv, keywords_file)
            end
            table.insert(argv, "-o")
            table.insert(argv, output_file)
            os.execv(vshaderc, argv)
        end

        if rebuild_desktop then
            build_vulkan_library("builtin_desktop.vshlib",
                                 shader_root_vulkan_desktop,
                                 keywords_vulkan_desktop,
                                 vshlib_desktop)
        else
            cprint("${cyan}[OK]${clear} builtin_desktop.vshlib")
        end

        if rebuild_android then
            build_vulkan_library("builtin_android.vshlib",
                                 shader_root_vulkan_android,
                                 keywords_vulkan_android,
                                 vshlib_android)
        else
            cprint("${cyan}[OK]${clear} builtin_android.vshlib")
        end

        if rebuild_webgpu then
            if enable_webgpu_shaderlib then
                build_webgpu_library("builtin_web.vshweblib",
                                     shader_root_webgpu,
                                     keywords_webgpu,
                                     vshweblib_web)
            else
                cprint("${yellow}[WARN]${clear} builtin_web.vshweblib disabled on Android, fallback to builtin_android.vshlib")
                os.cp(vshlib_android, vshweblib_web)
            end
        else
            cprint("${cyan}[OK]${clear} builtin_web.vshweblib")
        end

        ------------------------------------------------
        -- embed header
        ------------------------------------------------

        local rebuild_header = true

        local newest_lib_mtime = math.max(os.mtime(vshlib_desktop), os.mtime(vshlib_android), os.mtime(vshweblib_web))
        if os.exists(header)
        and os.mtime(header) >= newest_lib_mtime
        then
            rebuild_header = false
        end

        if rebuild_header then
            cprint("${green}[EMBED]${clear} builtin_shaders.hpp")

            local data_vshlib_desktop =
                io.readfile(vshlib_desktop, {encoding="binary"})
            local data_vshlib_android =
                io.readfile(vshlib_android, {encoding="binary"})
            local data_vshweblib_web =
                io.readfile(vshweblib_web, {encoding="binary"})

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

            write_embedded_blob("builtin_shaders_desktop_vshlib", data_vshlib_desktop)
            write_embedded_blob("builtin_shaders_android_vshlib", data_vshlib_android)
            write_embedded_blob("builtin_shaders_web_vshweblib", data_vshweblib_web)

            f:close()
        else
            cprint("${cyan}[OK]${clear} builtin_shaders.hpp")
        end
    end)
task_end()

-- Texture convert task
task("texture_task")
    on_run(function ()
        import("core.project.config")
        import("core.base.option")

        local projectdir = get_config("project_dir")

        local texture_root = path.join(projectdir, "builtin/textures")
		local texture_header_root = path.join(projectdir, "builtin/generated/include/texture_headers")
        os.mkdir(texture_header_root)

        -- valid texture formats to convert
        local exts = {"png", "jpg", "jpeg", "bmp", "tga", "psd", "gif", "hdr", "pic", "exr", "ktx", "ktx2", "dds"}

        local files = {}
        for _, ext in ipairs(exts) do
            local pattern = path.join(texture_root, "**" .. ext)
            table.join2(files, os.files(pattern))
        end

        for _, f in ipairs(files) do
            print(f)

            local rel = path.relative(f, texture_root)
            -- read texture binary and write to header file
            local tex_data = io.readfile(f, {encoding = "binary"})

            local header_path = path.join(texture_header_root, rel .. ".bintex.h")
            if os.exists(header_path) and os.mtime(header_path) >= os.mtime(f) then
                cprint("${cyan}[OK]${clear}   %s", rel)
            else
                cprint("${green}[CONVERT]${clear} %s", rel)
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
	end)
task_end()


task("font_task")
    on_run(function ()
        import("core.project.config")
        import("core.base.option")

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

        for _, f in ipairs(files) do
            local rel = path.relative(f, font_root)
            local header_path = path.join(font_header_root, rel .. ".binfont.h")

            -- check timestamp
            if os.exists(header_path) and os.mtime(header_path) >= os.mtime(f) then
                cprint("${cyan}[OK]${clear}   %s", rel)
            else
                cprint("${green}[CONVERT]${clear} %s", rel)
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
    end)
task_end()

if is_plat("android") then
    add_requires("vshadersystem v0.6.2", { configs = { debug = is_mode("debug") }})
else
    add_requires("vshadersystem v0.7.2", { configs = { debug = is_mode("debug") }})
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
