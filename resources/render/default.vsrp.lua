return RenderPipelineAsset {
    rendererKey = "universal",
    shaderLibraries = {
        project = "res://shaders/project.vshaderlib.lua",
    },
    features = {
        "direct_gbuffer",
        "general_gaussian_splat",
        "builtin_screen_space",
        "res://render/graphs/tonemapping.vrg.json",
        "final_composition",
    }
}
