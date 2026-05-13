target("example-openxr-gaussian-splatting")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    set_rundir("$(projectdir)")

    if is_plat("windows") then
        before_run(function ()
            local function valid_file(file)
                return file and file ~= "" and os.isfile(file)
            end

            local function add_file(candidates, file)
                if valid_file(file) then
                    table.insert(candidates, file)
                end
            end

            local function add_meta_xr_candidates(candidates, root)
                if not root or root == "" or not os.isdir(root) then
                    return
                end

                add_file(candidates, path.join(root, "v201.0", "meta_openxr_simulator.json"))
                for _, file in ipairs(os.files(path.join(root, "v*", "meta_openxr_simulator.json"))) do
                    add_file(candidates, file)
                end
                add_file(candidates, path.join(root, "meta_openxr_simulator.json"))
            end

            local runtime_candidates = {}
            add_file(runtime_candidates, os.getenv("META_XR_SIMULATOR_RUNTIME_JSON"))
            add_meta_xr_candidates(runtime_candidates, path.join(os.getenv("ProgramFiles") or "", "MetaXRSimulator"))
            add_meta_xr_candidates(runtime_candidates, path.join(os.getenv("ProgramFiles(x86)") or "", "MetaXRSimulator"))
            add_file(runtime_candidates, os.getenv("XR_RUNTIME_JSON"))

            local runtime_json = runtime_candidates[1]
            if not valid_file(runtime_json) then
                raise("Meta XR Simulator runtime manifest was not found. Set META_XR_SIMULATOR_RUNTIME_JSON or XR_RUNTIME_JSON.")
            end

            local simulator_exe = os.getenv("META_XR_SIMULATOR_EXE")
            if not valid_file(simulator_exe) then
                simulator_exe = path.join(path.directory(runtime_json), "MetaXRSimulator.exe")
            end
            if not valid_file(simulator_exe) then
                raise("Meta XR Simulator executable was not found. Set META_XR_SIMULATOR_EXE.")
            end

            os.setenv("XR_RUNTIME_JSON", runtime_json)

            local function ps_quote(value)
                return "'" .. value:gsub("'", "''") .. "'"
            end

            local warmup_seconds = tonumber(os.getenv("META_XR_SIMULATOR_WARMUP_SECONDS") or "6") or 6
            local script = "$ErrorActionPreference = 'Stop'; " ..
                           "$exe = " .. ps_quote(simulator_exe) .. "; " ..
                           "$warmup = " .. tostring(warmup_seconds) .. "; " ..
                           "$sim = Get-Process -Name 'MetaXRSimulator' -ErrorAction SilentlyContinue | Select-Object -First 1; " ..
                           "if (-not $sim) { " ..
                           "$sim = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru; " ..
                           "Start-Sleep -Seconds $warmup; " ..
                           "if ($sim.HasExited) { throw 'Meta XR Simulator exited during startup.' } " ..
                           "}"
            os.execv("powershell.exe", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script})
        end)
    end

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-openxr-gaussian-splatting")
