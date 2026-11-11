local is_root = (os.projectdir() == os.scriptdir())
local compile_commands_dir = "."
local featureError
if is_root then
    set_project("libvultra")
    set_version("0.1.0")
end
set_languages("cxx23")

if is_root then
    -- Running an example first checks its build and shared asset preparation dependencies.
    set_policy("run.autobuild", true)
end

-- global options
option("libvultra_features")
    set_default(is_root and "all" or "gpu")
    set_showmenu(true)
    set_description("Comma-separated Vultra modules; dependencies are selected automatically")
    after_check(function ()
        assert(not featureError, featureError)
    end)
option_end()

option("libvultra_graphics_api")
    set_default("vulkan")
    set_values("vulkan", "webgpu", "webgl2")
    set_showmenu(true)
    set_description("VRI backend: Vulkan on native platforms, WebGPU or WebGL2 on WebAssembly")
    after_check(function (option)
        local api = option:value()
        assert((is_plat("wasm") and (api == "webgpu" or api == "webgl2")) or
               (not is_plat("wasm") and api == "vulkan"), "Unsupported Vultra platform/graphics API selection")
    end)
option_end()

option("libvultra_vri_dir")
    set_default("")
    set_showmenu(true)
    set_description("Build VRI from this local checkout instead of the pinned archive")
option_end()

option("libvultra_examples")
    set_default("all")
    set_showmenu(true)
    set_description("Comma-separated example groups or modes, e.g. basics.vri,offline,ui")
option_end()

option("libvultra_example_pack")
    set_default("")
    set_showmenu(true)
    set_description("Host-cooked VPK embedded in Web examples that load shader programs")
option_end()

option("libvultra_shader_compiler")
    set_default(true)
    set_showmenu(true)
    set_description("Build the Slang compiler and shader hot reload; when off, shader loads cooked programs only")
option_end()

option("libvultra_asset_import")
    set_default(true)
    set_showmenu(true)
    set_description("Build model/texture importers, meshlet build and import jobs; when off, assets is values and VPK")
option_end()

option("libvultra_with_openxr")
    set_default(true)
    set_showmenu(true)
    set_description("Build the OpenXR module and example")
option_end()

option("libvultra_build_examples") -- build examples?
    set_default(is_root)
    set_showmenu(true)
    set_description("Enable vultra examples")
option_end()

option("libvultra_build_tests") -- build tests?
    set_default(is_root)
    set_showmenu(true)
    set_description("Enable vultra tests")
option_end()

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")

    -- Targets and packages must use the same MSVC runtime to avoid LNK2038.
    local msvc_runtime = is_mode("debug") and "MTd" or "MT"
    set_runtimes(msvc_runtime)

    -- set_runtimes() covers project targets; packages need the matching requirement.
    add_requireconfs("**", {configs = {runtimes = msvc_runtime}})
else
    add_cxxflags("-fexceptions")
end

-- Standalone editor tooling must not overwrite the parent project configuration.
if is_root then
    rule("clangd.config")
        on_config(function (target)
            if is_host("windows") then
                os.cp(".clangd.win", ".clangd")
            else
                os.cp(".clangd.nowin", ".clangd")
            end
        end)
    rule_end()
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.vsxmake.autoupdate")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = compile_commands_dir, lsp = "clangd"})
    add_rules("clangd.config")
end

rule("vultra.linux.delivery")
    on_config(function (target)
        if not target:is_plat("linux") or (target:kind() ~= "binary" and target:kind() ~= "shared") then
            return
        end
        local consumesVultra = false
        for _, dependency in ipairs(target:orderdeps()) do
            if dependency:name() == "vultra" then
                consumesVultra = true
                break
            end
        end
        if not consumesVultra then
            return
        end
        local links = {}
        for _, name in ipairs({"links", "syslinks"}) do
            for _, values in ipairs(target:get_from(name, "*") or {}) do
                table.join2(links, table.wrap(values))
            end
        end
        -- A trailing ldflag cannot remove unused desktop libraries. Apply it around the inherited links.
        target:add("linkgroups", table.unique(links), {name = "vultra_delivery", group = true, as_needed = true})
    end)
rule_end()

-- add repositories
add_repositories("vultra-packages " .. path.join(os.scriptdir(), "external", "packages"))
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

includes("examples/selection.lua")
includes("scripts/build_features.lua")
local features
features, featureError = vultraFeatures()
local shader_compiler = features.shader and has_config("libvultra_shader_compiler")
local asset_import = features.assets and has_config("libvultra_asset_import")
if has_config("libvultra_build_tests") then
    for _, name in ipairs({"render", "ui", "app"}) do
        if not features[name] then
            featureError = featureError or ("Repository tests require feature '" .. name ..
                           "'; select all or disable libvultra_build_tests")
        end
    end
    if not (shader_compiler and asset_import) then
        featureError = featureError or
                       "Repository tests require libvultra_shader_compiler and libvultra_asset_import"
    end
end

-- include external libraries
includes("external")

-- include source
includes("source")
if is_root then
    if asset_import or shader_compiler then
        includes("tools")
    end
    if shader_compiler and features.assets then
        includes("builtin")
    end
end

-- include tests
if has_config("libvultra_build_tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("libvultra_build_examples") then
    includes("examples")
end

