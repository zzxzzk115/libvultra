-- Example multi-input fullscreen project pass.
-- Reads the scene color ("source") and an engine resource ("depth") and binds them to
-- fragment set=3 bindings 0 and 1 respectively. Wire "depth" to a depth-producing pass
-- (e.g. DirectGBuffer.depth) in the render graph.
return RenderGraphPass {
    type = "DepthTint",
    inputs = { "source", "depth" },
    outputs = { "color" },
    shader = {
        vertexLibrary = "builtin",
        vertex = "fullscreen_triangle.vert",
        fragment = "depth_tint.frag",
    },
}
