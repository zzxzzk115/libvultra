-- Driver validation can report an error while the executable still exits successfully.
rule("vultra.test.diagnostics")
    after_test(function (target, testInfo)
        local output = (testInfo.stdout or "") .. "\n" .. (testInfo.stderr or "")
        local diagnostic = output:match("([^\n]*%[error%]%s+%[VRI%][^\n]*)") or
                           output:match("([^\n]*Validation Error[^\n]*)") or
                           output:match("([^\n]*SYNC%-HAZARD[^\n]*)")
        if diagnostic then
            local directory = path.join(os.projectdir(), get_config("builddir") or "build", ".tmp", "gpu-diagnostics")
            local file = path.join(directory, testInfo.name:gsub("[/\\]", "_") .. ".log")
            os.mkdir(directory)
            io.writefile(file, output)
            raise("%s: GPU validation failed: %s\nFull output: %s", testInfo.name, diagnostic, file)
        end
    end)
rule_end()
