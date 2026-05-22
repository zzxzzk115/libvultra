return RenderPipelineAsset {
    rendererKey = "universal",
    shaderLibraries = {
        project = "res://shaders/project.vshaderlib.lua",
    },
    features = {
        "compatibility_basecolor",
        "general_gaussian_splat",
        "res://render/graphs/tonemapping.vrg.json",
        "final_composition",
    }
}
