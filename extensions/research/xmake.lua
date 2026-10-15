-- Research algorithms and experiments are opt-in; neither the core nor the player depends on them.
local features = vultraFeatures()
target("vultra-research-extension")
    set_kind("static")
    set_default(false)
    if is_plat("linux", "macosx") then
        add_cxflags("-fPIC")
    end
    add_deps("vultra", {public = true})
    add_includedirs("include", {public = true})
    if features.research_quality then
        add_deps("flip")
        add_headerfiles("include/(vultra/research/quality.hpp)")
        add_files("src/quality.cpp")
    end
    if features.research_graph then
        add_headerfiles("include/(vultra/research/graph_report.hpp)")
        add_files("src/graph_report.cpp")
    end
    if features.research_camera then
        add_headerfiles("include/(vultra/research/camera_track.hpp)")
        add_files("src/camera_track.cpp")
    end
    if features.research_project then
        add_headerfiles("include/(vultra/research/research_project.hpp)")
        add_files("src/research_project.cpp")
    end
    if features.research_reference then
        add_headerfiles("include/(vultra/research/reference_path_tracer.hpp)",
                        "include/(vultra/research/environment_sampling.hpp)")
        add_files("src/reference_path_tracer.cpp", "src/environment_sampling.cpp")
    end
    if features.research_experiment then
        add_headerfiles("include/(vultra/research/experiment_session.hpp)",
                        "include/(vultra/research/experiment_description.hpp)")
        add_files("src/experiment_session.cpp", "src/experiment_description.cpp")
    end
    if features.research_api then
        add_deps("vultra-native-sdk", {public = true})
        add_headerfiles("include/(vultra/research/api/**.h)", "include/(vultra/research/api/research_bridge.hpp)")
        add_files("src/research_bridge.cpp")
    end
    if features.research_stereo then
        add_headerfiles("include/(vultra/research/stereo_views.hpp)",
                        "include/(vultra/research/stereo_renderer.hpp)",
                        "include/(vultra/research/headset_profile.hpp)",
                        "include/(vultra/research/research_configuration.hpp)")
        add_files("src/stereo_views.cpp", "src/stereo_renderer.cpp", "src/headset_profile.cpp",
                  "src/research_configuration.cpp")
    end
    if features.json then
        add_packages("nlohmann_json")
    end
target_end()

if not features.research_host then
    return
end

-- The same generated ABI serves scripting hosts; plain C++ research can omit Lua and .NET.
target("vultra-research-scripting")
    set_kind("static")
    set_default(false)
    if is_plat("linux", "macosx") then
        add_cxflags("-fPIC")
    end
    add_deps("vultra-research-extension", "vultra-scripting", {public = true})
    add_includedirs("scripting/include", {public = true})
    add_headerfiles("scripting/include/(vultra/**.hpp)")
    add_files("scripting/src/*.cpp")
    add_packages("nlohmann_json")
target_end()
