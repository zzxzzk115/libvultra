-- Example custom XR view-synthesis WARP backend (scripted project pass).
--
-- A warp backend is just a render-graph node with the contract:
--   inputs  = { "source", "depth" }   -- scene color + scene depth
--   outputs = { "color" }             -- warped color, ALPHA = validity
--
-- Wire this node in a .vrg.json synthesis graph in place of XrGeometryWarp, then
-- feed its "color" output into XrPullPushInpaint (or your own inpaint backend).
-- The alpha-validity convention is documented in
-- ai/specs/xr-view-synthesis-rendergraph.md and in xr_custom_warp.frag.
--
-- This example is fullscreen (no geometry shader) so it also runs on WebGPU,
-- where the built-in geometry warp is unavailable. It pairs with the default
-- depth-aware pull-push (it writes depth-aware alpha).
local state = {}

return RenderGraphPass {
    type     = "XrCustomWarp",
    menuPath = "XR/Custom Warp (example)",
    inputs  = { "source", "depth" },
    outputs = { "color" },

    shader = { fragmentLibrary = "project", fragment = "project/fullscreen/xr_custom_warp.frag" },

    setup = function(ctx)
        local src   = ctx:getInput("source")
        local depth = ctx:getInput("depth")
        ctx:read(src,   { set = 3, binding = 0, stage = "fragment" })
        ctx:read(depth, { set = 3, binding = 1, stage = "fragment", depth = true })
        local out = ctx:createColorTexture { name = "XrCustomWarp Color", inherit = src }
        ctx:writeColor(out)
        ctx:setOutput("color", out)
        ctx:useGraphicsShader {
            vertexLibrary   = "builtin",
            vertex          = "builtin/general/fullscreen_triangle.vert",
            fragmentLibrary = "project",
            fragment        = "project/fullscreen/xr_custom_warp.frag",
        }
        -- Params + their range/enum constraints come from the shader [properties] block
        -- (disparityScale range(0,0.2), warpDirection enum(...)); the editor reflects them as a
        -- bounded slider and an enum dropdown. enum values are int-backed -> paramInt.
        state.disparityScale = ctx:paramFloat("disparityScale", 0.035)
        state.warpDirection  = ctx:paramInt("warpDirection", 0)
    end,

    execute = function(rc)
        if not rc:bindPipeline() then return end
        rc:bindDescriptorSets()
        rc:pushConstants("fragment", { disparityScale = state.disparityScale, warpDirection = state.warpDirection })
        rc:beginRendering()
        rc:drawFullscreen()
        rc:endRendering()
    end,
}
