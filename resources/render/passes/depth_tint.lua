-- Depth-tint post-processing pass (scripted: the project pass standard).
-- Multi-input example: reads scene color ("source") + an engine resource
-- ("depth") and binds them to fragment set=3 bindings 0 and 1. Wire "depth" to a
-- depth-producing pass (e.g. DirectGBuffer.depth) in the render graph.
local state = {}

return RenderGraphPass {
    type     = "DepthTint",
    menuPath = "Debug/Depth Tint",
    inputs  = { "source", "depth" },
    outputs = { "color" },

    -- Auto-exposes depth_tint.frag's reflected params on the graph node.
    shader = { fragmentLibrary = "project", fragment = "project/fullscreen/depth_tint.frag" },

    setup = function(ctx)
        local src   = ctx:getInput("source")
        local depth = ctx:getInput("depth")
        ctx:read(src, { set = 3, binding = 0, stage = "fragment" })
        ctx:read(depth, { set = 3, binding = 1, stage = "fragment", depth = true })
        local out = ctx:createColorTexture { name = "DepthTint Color", inherit = src }
        ctx:writeColor(out)
        ctx:setOutput("color", out)
        ctx:useGraphicsShader {
            vertexLibrary   = "builtin",
            vertex          = "builtin/general/fullscreen_triangle.vert",
            fragmentLibrary = "project",
            fragment        = "project/fullscreen/depth_tint.frag",
        }
        state.bandScale = ctx:paramFloat("bandScale", 24.0)
        state.strength  = ctx:paramFloat("strength", 0.75)
    end,

    execute = function(rc)
        if not rc:bindPipeline() then return end
        rc:bindDescriptorSets()
        rc:pushConstants("fragment", { bandScale = state.bandScale, strength = state.strength })
        rc:beginRendering()
        rc:drawFullscreen()
        rc:endRendering()
    end,
}
