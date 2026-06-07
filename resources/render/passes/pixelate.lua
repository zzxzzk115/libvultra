-- Pixelate post-processing pass (scripted: the project pass standard).
-- A full SRP pass authored in Lua: setup declares FrameGraph I/O + picks the
-- shader; execute records the fullscreen draw. See doc/scripted_render_passes.md.
local state = {}

return RenderGraphPass {
    type     = "Pixelate",
    menuPath = "Post Processing/Pixelate",
    inputs  = { "source" },
    outputs = { "color" },

    -- Auto-exposes pixelate.frag's reflected params on the graph node.
    shader = { fragmentLibrary = "project", fragment = "project/fullscreen/pixelate.frag" },

    setup = function(ctx)
        local src = ctx:getInput("source")
        ctx:read(src, { set = 3, binding = 0, stage = "fragment" })
        local out = ctx:createColorTexture { name = "Pixelate Color", inherit = src }
        ctx:writeColor(out)
        ctx:setOutput("color", out)
        ctx:useGraphicsShader {
            vertexLibrary   = "builtin",
            vertex          = "builtin/general/fullscreen_triangle.vert",
            fragmentLibrary = "project",
            fragment        = "project/fullscreen/pixelate.frag",
        }
        state.pixelSize     = ctx:paramFloat("pixelSize", 8.0)
        state.gridOffset    = ctx:paramFloat("gridOffset", 0.5)
        state.mode          = ctx:paramInt("mode", 0)
        state.preserveAlpha = ctx:paramBool("preserveAlpha", true) and 1 or 0
    end,

    execute = function(rc)
        if not rc:bindPipeline() then return end
        rc:bindDescriptorSets()
        rc:pushConstants("fragment", {
            pixelSize     = state.pixelSize,
            gridOffset    = state.gridOffset,
            mode          = state.mode,
            preserveAlpha = state.preserveAlpha,
        })
        rc:beginRendering()
        rc:drawFullscreen()
        rc:endRendering()
    end,
}
