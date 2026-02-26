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
        import("lib.detect.find_tool")

        local target = project.target("vultra_builtin_assets")
        assert(target, "target not found")

        local pkg = target:pkg("vshadersystem")
        assert(pkg, "vshadersystem package not found")

        local vshaderc_exe = path.join(pkg:installdir(), "bin", "vshaderc")
        assert(vshaderc_exe, "vshaderc not found in package")

        local projectdir = get_config("project_dir")

        local shader_root = path.join(projectdir, "builtin/shaders")
        local bin_root    = path.join(projectdir, "builtin/shader_bins")
        local lib_root    = path.join(projectdir, "builtin/shader_lib")
        local header_root = path.join(projectdir, "builtin/generated/include")

        local keywords_file =
            path.join(projectdir, "builtin/shaders/builtin_keywords.vkw")

        local vshlib =
            path.join(lib_root, "builtin.vshlib")

        local header =
            path.join(header_root, "builtin_shaders.hpp")

        os.mkdir(bin_root)
        os.mkdir(lib_root)
        os.mkdir(header_root)

        ------------------------------------------------------------
        -- find .vshader files
        ------------------------------------------------------------

        local stages =
        {
            vert = true,
            frag = true,
            comp = true,
            task = true,
            mesh = true,
            rgen = true,
            rmiss = true,
            rchit = true,
            rahit = true,
            rint = true
        }

        local shader_files =
            os.files(path.join(shader_root, "**.vshader"))

        local vshbins = {}

        ------------------------------------------------------------
        -- compile .vshader → .vshbin
        ------------------------------------------------------------

        for _, file in ipairs(shader_files)
        do

            local rel =
                path.relative(file, shader_root)

            local filename = path.filename(file)
            local stage = filename:match("%.([^.]+)%.vshader$")

            print(stage)

            if not stages[stage] then
                raise("Unknown shader stage: %s", rel)
            end

            local out =
                path.join(bin_root, rel .. ".vshbin")

            os.mkdir(path.directory(out))

            table.insert(vshbins, out)

            if os.exists(out) and os.mtime(out) >= os.mtime(file)
            then

                cprint("${cyan}[OK]${clear}   %s", rel)

            else

                cprint("${green}[BUILD]${clear} %s", rel)

                os.execv(vshaderc_exe,
                {
                    "compile",

                    "-i", file,

                    "-o", out,

                    "-S", stage,

                    "-I", shader_root,

                    "--keywords-file", keywords_file
                })

            end
        end

        ------------------------------------------------------------
        -- pack → builtin.vshlib
        ------------------------------------------------------------

        local rebuild_lib = true

        if os.exists(vshlib)
        then
            rebuild_lib = false

            for _, bin in ipairs(vshbins)
            do
                if os.mtime(bin) > os.mtime(vshlib)
                then
                    rebuild_lib = true
                    break
                end
            end
        end

        if rebuild_lib then

            cprint("${green}[PACK]${clear} builtin.vshlib")

            local argv =
            {
                "packlib",
                "-o", vshlib,
                "--keywords-file", keywords_file
            }

            table.join2(argv, vshbins)

            os.execv(vshaderc_exe, argv)

        else

            cprint("${cyan}[OK]${clear}   builtin.vshlib")

        end

        ------------------------------------------------------------
        -- embed → builtin_shaders.hpp
        ------------------------------------------------------------

        local rebuild_header = true

        if os.exists(header)
            and os.mtime(header) >= os.mtime(vshlib)
        then
            rebuild_header = false
        end

        if rebuild_header then

            cprint("${green}[EMBED]${clear} builtin_shaders.hpp")

            local data =
                io.readfile(vshlib, {encoding = "binary"})

            local f =
                io.open(header, "w")

            f:write("// auto-generated\n")
            f:write("#pragma once\n\n")
            f:write("#include <cstdint>\n\n")

            f:write("inline constexpr uint8_t builtin_shaders_vshlib[] = {\n")

            for i = 1, #data
            do

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

            f:write(
                string.format(
                    "inline constexpr size_t builtin_shaders_vshlib_size = %d;\n",
                    #data))

            f:close()

        else

            cprint("${cyan}[OK]${clear}   builtin_shaders.hpp")

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

add_requires("vshadersystem")

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