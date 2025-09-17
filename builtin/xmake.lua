-- Shader compile task
task("shader_task")
    on_run(function ()
        import("core.project.config")
        import("core.base.option")

        local shader_root = path.join(os.projectdir(), "builtin/shaders")
        local spv_root = path.join(os.projectdir(), "builtin/shader_spvs")
		local shader_header_root = path.join(os.projectdir(), "builtin/generated/include/shader_headers")
        os.mkdir(spv_root)

        -- valid shader stages to compile
        local stages = {".vert", ".frag", ".comp", ".geom", ".tesc", ".tese",
                        ".mesh", ".task", ".rgen", ".rmiss", ".rahit", ".rchit",
                        ".rint", ".rcall"}

        local files = {}
        for _, ext in ipairs(stages) do
            local pattern = path.join(shader_root, "**" .. ext)
            table.join2(files, os.files(pattern))
        end

        for _, f in ipairs(files) do
			print(f)
            local rel = path.relative(f, shader_root)
            local out_spv = path.join(spv_root, rel .. ".spv")
            os.mkdir(path.directory(out_spv))

            -- incremental build: skip if up-to-date
            if os.exists(out_spv) and os.mtime(out_spv) >= os.mtime(f) then
                cprint("${cyan}[OK]${clear}   %s", rel)
            else
                cprint("${green}[BUILD]${clear} %s", rel)
                os.execv("glslangValidator", {
                    "-V",
					"--target-env", "vulkan1.2",
                    "-I" .. shader_root,
                    "-o", out_spv,
					"-P#extension GL_ARB_shading_language_include : enable", -- https://github.com/KhronosGroup/glslang/issues/1691#issuecomment-2282322200
					"-P#extension GL_GOOGLE_cpp_style_line_directive : require",
					"-P#define DEPTH_ZERO_TO_ONE 1", -- for Vulkan Depth
                    f
                })

                -- read spv binary and write to header file
                local spv_data = io.readfile(out_spv, {encoding = "binary"})

                -- magic number check
                if #spv_data < 4 then
                    raise("spv file too small: %s", out_spv)
                end

                local b1, b2, b3, b4 = spv_data:byte(1, 4)
                local magic = b1 | (b2 << 8) | (b3 << 16) | (b4 << 24)

                if magic ~= 0x07230203 then
                    raise("invalid SPIR-V magic number in %s: got 0x%08X, expected 0x07230203", out_spv, magic)
                end

                local header_path = path.join(shader_header_root, rel .. ".spv.h")
                os.mkdir(path.directory(header_path))

                -- base name + extension -> unique symbol
                local base = path.basename(rel):gsub("%.", "_")
                local ext  = path.extension(rel):sub(2)
                local symbol = base .. "_" .. ext -- e.g. pbr_vert

                local header_file = io.open(header_path, "w")
                header_file:write("// Auto-generated from " .. rel .. "\n")
                header_file:write("#pragma once\n\n")
                header_file:write("#include <cstdint>\n")
                header_file:write("#include <vector>\n\n")
                header_file:write("const std::vector<uint32_t> " .. symbol .. "_spv = {\n    ")

                for i = 1, #spv_data, 4 do
                    if i > 1 then header_file:write(",\n    ") end
                    local w1, w2, w3, w4 = spv_data:byte(i, i + 3)
                    local word = w1 | (w2 << 8) | (w3 << 16) | (w4 << 24)
                    header_file:write(string.format("0x%08X", word))
                end

                header_file:write("\n};\n")
                header_file:close()
            end
		end
	end)
task_end()


-- Texture convert task
task("texture_task")
    on_run(function ()
        import("core.project.config")
        import("core.base.option")

        local texture_root = path.join(os.projectdir(), "builtin/textures")
		local texture_header_root = path.join(os.projectdir(), "builtin/generated/include/texture_headers")
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


target("vultra_builtin_assets")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")

	add_headerfiles("generated/include/**.h")
    add_includedirs("generated/include", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

    on_config(function (target)
        import("core.base.task")
        task.run("shader_task")
        task.run("texture_task")
    end)