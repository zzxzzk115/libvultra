-- Builds the vultra library once per module, each selected alone with only its declared dependencies.
--
--     xmake lua tests/build_modules.lua [module...]
--
-- Every case has its own configuration and build directory under build/.tmp/modules/<module>; the main build/
-- configuration is not touched. Platform, architecture, mode and the VRI checkout are read from the main
-- configuration. Pass module names to build a subset.
import("core.project.config")

local modules = {"core", "platform", "gpu", "shader", "graph", "assets", "render", "ui", "app", "xr"}

-- Configures and builds one module; the output of both steps goes to its build.log.
local function build(module, output)
    local directory = path.join(output, module)
    os.mkdir(directory)
    local configure = {"f", "-y", "-p", config.get("plat"), "-a", config.get("arch"), "-m", config.get("mode"),
                       "-o", path.join(directory, "build"), "--libvultra_features=" .. module,
                       "--libvultra_build_tests=n", "--libvultra_build_examples=n"}
    local vri = config.get("libvultra_vri_dir")
    if vri and vri ~= "" then
        table.insert(configure, "--libvultra_vri_dir=" .. vri)
    end
    local log = io.open(path.join(directory, "build.log"), "w")
    local passed = true
    for _, arguments in ipairs({configure, {"build", "-y", "vultra"}}) do
        local code = os.execv("xmake", arguments, {try = true, stdout = log, stderr = log, curdir = os.projectdir(),
                                                   envs = {XMAKE_CONFIGDIR = directory}})
        if code ~= 0 then
            passed = false
            break
        end
    end
    log:close()
    return passed
end

function main(...)
    local selected = {...}
    if #selected == 0 then
        selected = modules
    end
    for _, module in ipairs(selected) do
        if not table.contains(modules, module) then
            raise("Unknown module: " .. module)
        end
    end
    config.load()
    if not config.get("plat") then
        raise("Configure the main project first; its platform selects the module builds")
    end
    local output = path.join(os.projectdir(), "build", ".tmp", "modules")
    os.mkdir(output)
    -- The root project regenerates compile_commands.json for whichever configuration built last.
    local commands = path.join(os.projectdir(), "compile_commands.json")
    local saved = path.join(output, "compile_commands.json")
    if os.isfile(commands) then
        os.cp(commands, saved)
    end
    local failed = {}
    for _, module in ipairs(selected) do
        local passed = build(module, output)
        print(module .. ": " .. (passed and "pass" or "FAIL") .. " (" .. path.join(output, module, "build.log") .. ")")
        if not passed then
            table.insert(failed, module)
        end
    end
    if os.isfile(saved) then
        os.cp(saved, commands)
    end
    if #failed > 0 then
        raise("Modules that do not build alone: " .. table.concat(failed, ", "))
    end
end
