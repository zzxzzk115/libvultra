rule("bc7enc.ispc")
    set_extensions(".ispc")
    on_config(function (target)
        target:add("includedirs", path.join(target:autogendir(), "bc7-ispc"), {public = true})
    end)
    before_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        import("core.project.project")
        local package = project.required_package("ispc")
        local compiler = path.join(package:installdir(), "bin", is_host("windows") and "ispc.exe" or "ispc")
        assert(os.isfile(compiler), "Missing ISPC package executable: " .. compiler)
        local object = target:objectfile(sourcefile)
        local headerdir = path.join(target:autogendir(), "bc7-ispc")
        table.insert(target:objectfiles(), object)
        local isas
        local arch
        if target:is_arch("arm64", "aarch64") then
            -- A single NEON target emits one object without a dispatcher.
            isas = {}
            arch = {"--target=neon-i32x4", "--arch=aarch64"}
        else
            -- Multi-target ISPC emits a dispatcher and a separate object for each ISA.
            isas = {"sse2", "avx2"}
            arch = {"--target=sse2-i32x4,avx2-i32x8", "--arch=x86-64"}
        end
        for _, isa in ipairs(isas) do
            table.insert(target:objectfiles(), path.join(path.directory(object),
                path.basename(object) .. "_" .. isa .. path.extension(object)))
        end
        -- ISPC names macOS "macos" while xmake names it "macosx".
        local targetos = target:is_plat("macosx") and "macos" or target:plat()
        local flags = {arch[1], arch[2], "--target-os=" .. targetos,
                       "-O2", "--opt=disable-assertions", "-h", path.join(headerdir, "bc7e_ispc.h"),
                       "-o", object, sourcefile}
        if target:is_plat("linux") then
            table.insert(flags, "--pic")
        end
        batchcmds:show_progress(opt.progress, "${color.build.object}compiling.bc7-ispc %s", sourcefile)
        batchcmds:mkdir(path.directory(object))
        batchcmds:mkdir(headerdir)
        batchcmds:vrunv(compiler, flags)
        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(object))
        batchcmds:set_depcache(target:dependfile(object))
    end)
rule_end()

target("bc7enc")
    set_kind("static")
    set_default(false)
    -- Consumers include the generated header, so generation must finish before their compilation.
    set_policy("build.fence", true)
    add_packages("ispc")
    add_rules("c", "bc7enc.ispc") -- Export native-library link metadata for this ISPC-only target.
    add_files("bc7e.ispc")
target_end()
