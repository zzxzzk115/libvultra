-- Shared Python venv provisioning for build-time code generation.
--
-- Provisions a project-local .venv (gitignored) from tools/python/requirements.txt
-- using only a system `python3`/`python` binary, and returns the path to the
-- venv's python interpreter. Installation is stamp-guarded so it runs once and
-- again only when requirements.txt changes -- subsequent builds are no-ops.
--
-- Reused by every build-time python tool (gen_lua_bindings, gen_imgui_lua, ...)
-- so they share one environment. Callers:
--   import("python_venv", { rootdir = path.join(os.projectdir(), "xmake") })
--   local py = python_venv()            -- raises on failure
--   local py = python_venv({soft=true}) -- returns nil instead of raising
--
-- On `soft = true` any provisioning failure (no python, offline pip) returns
-- nil so the caller can fall back to the checked-in generated files rather
-- than breaking the build.

import("lib.detect.find_tool")

function _venv_python(venvdir)
    if is_host("windows") then
        return path.join(venvdir, "Scripts", "python.exe")
    end
    return path.join(venvdir, "bin", "python")
end

function main(opt)
    opt = opt or {}
    local venvdir = path.join(os.projectdir(), ".venv")
    local pybin   = _venv_python(venvdir)
    local req     = path.join(os.projectdir(), "tools", "python", "requirements.txt")
    local stamp   = path.join(venvdir, ".requirements.stamp")

    local function bail(msg)
        if opt.soft then
            cprint("${color.warning}[python_venv] %s; using checked-in generated files", msg)
            return nil
        end
        raise(msg)
    end

    -- 1. create the venv from a host python (binary only) if missing
    if not os.isfile(pybin) then
        local py3 = find_tool("python3") or find_tool("python")
        if not py3 then
            return bail("no system python3/python found to create .venv")
        end
        local ok = try
        {
            function()
                os.vrunv(py3.program, {"-m", "venv", venvdir})
                return true
            end
        }
        if not ok or not os.isfile(pybin) then
            return bail("failed to create .venv (python -m venv)")
        end
    end

    -- 2. install/refresh requirements, stamp-guarded against requirements.txt mtime
    if not os.isfile(stamp) or (os.isfile(req) and os.mtime(req) > os.mtime(stamp)) then
        local ok = try
        {
            function()
                os.vrunv(pybin, {"-m", "pip", "install", "-r", req,
                                 "--quiet", "--disable-pip-version-check"})
                return true
            end
        }
        if not ok then
            return bail("pip install -r requirements.txt failed (offline?)")
        end
        io.writefile(stamp, os.date("%Y-%m-%d %H:%M:%S"))
    end

    return pybin
end
