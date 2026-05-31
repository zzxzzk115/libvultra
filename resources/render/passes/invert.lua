return RenderGraphPass {
    type = "Invert",
    pipeline = "compute",
    inputs = { "source" },
    outputs = { "color" },
    shader = {
        library = "project",
        compute = "invert.comp",
    },
    dispatch = {
        byOutputSize = true,
    },
}
