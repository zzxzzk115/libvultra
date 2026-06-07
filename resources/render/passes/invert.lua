-- Invert compute pass (scripted: the project pass standard).
-- A compute SRP pass authored in Lua: setup reads the source, allocates a storage
-- output, and picks the compute shader; execute dispatches one group per output
-- texel block. See doc/scripted_render_passes.md.
local state = {}

return RenderGraphPass {
    type     = "Invert",
    menuPath = "Post Processing/Invert",
    inputs  = { "source" },
    outputs = { "color" },

    -- Auto-exposes invert.comp's reflected params on the graph node.
    shader = { compute = "project/compute/invert.comp" },

    setup = function(ctx)
        local src = ctx:getInput("source")
        ctx:read(src, { set = 3, binding = 0, stage = "compute" })
        local out = ctx:createColorTexture { name = "Invert Color", inherit = src, storage = true }
        ctx:writeStorage(out, { set = 3, binding = 1, stage = "compute" })
        ctx:setOutput("color", out)
        ctx:useComputeShader { library = "project", compute = "project/compute/invert.comp" }
        state.strength      = ctx:paramFloat("strength", 1.0)
        state.preserveAlpha = ctx:paramBool("preserveAlpha", true) and 1 or 0
        state.mode          = ctx:paramInt("mode", 0)
    end,

    execute = function(rc)
        if not rc:bindPipeline() then return end
        rc:bindDescriptorSets()
        rc:pushConstants("compute", {
            strength      = state.strength,
            preserveAlpha = state.preserveAlpha,
            mode          = state.mode,
        })
        rc:dispatchByOutputSize()
    end,
}
