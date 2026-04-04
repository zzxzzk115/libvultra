add_requires("webgpu-sdk v0.1.0")

target("example-rhi-triangle-webgpu-sdk")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("webgpu-sdk")
    if is_plat("wasm") then
        add_cflags("--use-port=emdawnwebgpu", {force = true})
        add_cxxflags("--use-port=emdawnwebgpu", {force = true})
        add_ldflags("--use-port=emdawnwebgpu", "-sUSE_GLFW=3", "-sASYNCIFY", {force = true})
    end
    on_load(function (target)
        local pkg = target:pkg("webgpu-sdk")
        if pkg then
            target:add("rpathdirs", path.join(pkg:installdir(), "lib"))
        end
    end)

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-rhi-triangle-webgpu-sdk")
