rule("vultra.bc7-ispc")
    set_extensions(".ispc")
    on_config(function (target)
        target:add("includedirs", path.join(target:autogendir(), "bc7-ispc"))
    end)
    before_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        import("core.project.project")
        local package = project.required_package("ispc")
        local compiler = path.join(package:installdir(), "bin", "ispc.exe")
        assert(os.isfile(compiler), "Missing ISPC package executable: " .. compiler)
        local object = target:objectfile(sourcefile)
        local headerdir = path.join(target:autogendir(), "bc7-ispc")
        -- Multi-target ISPC emits a dispatcher and a separate object for each ISA.
        table.insert(target:objectfiles(), object)
        for _, isa in ipairs({"sse2", "avx2"}) do
            table.insert(target:objectfiles(), path.join(path.directory(object),
                path.basename(object) .. "_" .. isa .. path.extension(object)))
        end
        local flags = {"--target=sse2-i32x4,avx2-i32x8", "--arch=x86-64", "--target-os=windows",
                       "-O2", "--opt=disable-assertions", "-h", path.join(headerdir, "bc7e_ispc.h"),
                       "-o", object, sourcefile}
        batchcmds:show_progress(opt.progress, "${color.build.object}compiling.bc7-ispc %s", sourcefile)
        batchcmds:mkdir(path.directory(object))
        batchcmds:mkdir(headerdir)
        batchcmds:vrunv(compiler, flags)
        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(object))
        batchcmds:set_depcache(target:dependfile(object))
    end)
rule_end()
