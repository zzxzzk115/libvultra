return RenderGraphPass {
    type = "Pixelate",
    inputs = { "source" },
    outputs = { "color" },
    shader = {
        vertexLibrary = "builtin",
        vertex = "fullscreen_triangle.vert",
        fragment = "pixelate.frag",
    },
}
