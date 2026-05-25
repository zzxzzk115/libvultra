return RenderGraphPass {
    type = "Pixelate",
    shader = {
        library = "project",
        vertex = "fullscreen_triangle.vert",
        fragment = "pixelate.frag",
    },
}
