#include "editor_app/content_asset_registry.hpp"

#include <algorithm>

namespace vultra_app
{
    namespace
    {
        std::string makeSceneText(std::string_view)
        {
            return R"([vscn]
version = 1
root    = 0
)";
        }

        std::string makeLuaScriptText(std::string_view)
        {
            return R"(function OnCreate(self)
end

function OnUpdate(self, dt)
end
)";
        }

        std::string makeSurfaceShaderText(std::string_view)
        {
            return R"([vshader]
language = glsl
version = 460

[vert]
layout(location = 0) out vec2 v_TexCoord;

void main()
{
    v_TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_TexCoord * 2.0 - 1.0, 0.0, 1.0);
}

[frag]
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = vec4(v_TexCoord, 1.0, 1.0);
}
)";
        }

        std::string makePostProcessingShaderText(std::string_view)
        {
            return R"([vshader]
language = glsl
version = 460

[frag]
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform sampler2D u_Source;

void main()
{
    vec4 color = texture(u_Source, v_TexCoord);
    FragColor = vec4(color.rgb, color.a);
}
)";
        }

        std::string makeComputeShaderText(std::string_view)
        {
            return R"([vshader]
language = glsl
version = 460

[comp]
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform texture2D u_Source;
layout(set = 3, binding = 1, rgba16f) uniform writeonly image2D u_Output;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_Output);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    imageStore(u_Output, pixel, texelFetch(u_Source, pixel, 0));
}
)";
        }

        std::string makeRayTracingShaderText(std::string_view)
        {
            return R"([vshader]
language = glsl
version = 460

[rgen]
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadEXT vec4 payload;

void main()
{
    payload = vec4(0.0, 0.0, 0.0, 1.0);
}

[rmiss]
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

void main()
{
    payload = vec4(0.02, 0.04, 0.08, 1.0);
}

[rchit]
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

void main()
{
    payload = vec4(1.0, 0.65, 0.25, 1.0);
}
)";
        }

        std::string makeRenderPassText(std::string_view)
        {
            return R"(return RenderGraphPass {
    type = "NewRenderPass",
    inputs = { "source" },
    outputs = { "color" },
    shader = {
        library = "project",
        vertexLibrary = "builtin",
        vertex = "fullscreen_triangle.vert",
        fragment = "pixelate.frag",
    },
}
)";
        }

        std::string makeComputePassText(std::string_view)
        {
            return R"(return RenderGraphPass {
    type = "NewComputePass",
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
)";
        }

        std::string makeRayTracingPassText(std::string_view)
        {
            return R"(return RenderGraphPass {
    type = "NewRayTracingPass",
    pipeline = "raytracing",
    inputs = { "source" },
    outputs = { "color" },
    shader = {
        library = "project",
        raygen = "default_rt_primary.rgen",
    },
}
)";
        }
    } // namespace

    ContentAssetRegistry& ContentAssetRegistry::instance()
    {
        static ContentAssetRegistry registry;
        return registry;
    }

    bool ContentAssetRegistry::registerCreator(ContentAssetCreator creator)
    {
        if (creator.id.empty() || creator.menuPath.empty() || creator.defaultFileName.empty() ||
            creator.extension.empty() || !creator.makeText)
        {
            return false;
        }

        auto& creators = m_Creators;
        const auto it = std::find_if(creators.begin(), creators.end(), [&](const ContentAssetCreator& existing) {
            return existing.id == creator.id;
        });
        if (it != creators.end())
            *it = std::move(creator);
        else
            creators.push_back(std::move(creator));

        std::stable_sort(creators.begin(), creators.end(), [](const auto& a, const auto& b) {
            return a.menuPath < b.menuPath;
        });
        return true;
    }

    const ContentAssetCreator* ContentAssetRegistry::find(std::string_view id) const
    {
        const auto it = std::find_if(m_Creators.begin(), m_Creators.end(), [&](const ContentAssetCreator& creator) {
            return creator.id == id;
        });
        return it == m_Creators.end() ? nullptr : &*it;
    }

    void registerBuiltinContentAssetCreators()
    {
        static bool registered = false;
        if (registered)
            return;

        auto& registry = ContentAssetRegistry::instance();
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.scene",
            .menuPath        = "Scene",
            .displayName     = "Scene",
            .defaultFileName = "NewScene.vscn",
            .extension       = ".vscn",
            .assetType       = vasset::VAssetType::eScene,
            .openInCodeEditor = false,
            .makeText        = makeSceneText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.lua_script",
            .menuPath        = "Script/Lua Script",
            .displayName     = "Lua Script",
            .defaultFileName = "NewScript.lua",
            .extension       = ".lua",
            .assetType       = vasset::VAssetType::eScriptLua,
            .openInCodeEditor = true,
            .makeText        = makeLuaScriptText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.render_pass",
            .menuPath        = "Render Graph/Render Pass",
            .displayName     = "Render Pass",
            .defaultFileName = "NewRenderPass.lua",
            .extension       = ".lua",
            .assetType       = vasset::VAssetType::eScriptLua,
            .openInCodeEditor = true,
            .makeText        = makeRenderPassText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.post_processing_pass",
            .menuPath        = "Render Graph/Post Processing Pass",
            .displayName     = "Post Processing Pass",
            .defaultFileName = "NewPostProcessingPass.lua",
            .extension       = ".lua",
            .assetType       = vasset::VAssetType::eScriptLua,
            .openInCodeEditor = true,
            .makeText        = makeRenderPassText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.compute_pass",
            .menuPath        = "Render Graph/Compute Pass",
            .displayName     = "Compute Pass",
            .defaultFileName = "NewComputePass.lua",
            .extension       = ".lua",
            .assetType       = vasset::VAssetType::eScriptLua,
            .openInCodeEditor = true,
            .makeText        = makeComputePassText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.raytracing_pass",
            .menuPath        = "Render Graph/Raytracing Pass",
            .displayName     = "Raytracing Pass",
            .defaultFileName = "NewRaytracingPass.lua",
            .extension       = ".lua",
            .assetType       = vasset::VAssetType::eScriptLua,
            .openInCodeEditor = true,
            .makeText        = makeRayTracingPassText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.surface_shader",
            .menuPath        = "Shader/Surface Shader",
            .displayName     = "Surface Shader",
            .defaultFileName = "NewSurface.vshader",
            .extension       = ".vshader",
            .assetType       = vasset::VAssetType::eUnknown,
            .openInCodeEditor = true,
            .makeText        = makeSurfaceShaderText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.post_processing_shader",
            .menuPath        = "Shader/Post Processing Shader",
            .displayName     = "Post Processing Shader",
            .defaultFileName = "NewPostProcess.frag.vshader",
            .extension       = ".vshader",
            .assetType       = vasset::VAssetType::eUnknown,
            .openInCodeEditor = true,
            .makeText        = makePostProcessingShaderText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.compute_shader",
            .menuPath        = "Shader/Compute Shader",
            .displayName     = "Compute Shader",
            .defaultFileName = "NewCompute.comp.vshader",
            .extension       = ".vshader",
            .assetType       = vasset::VAssetType::eUnknown,
            .openInCodeEditor = true,
            .makeText        = makeComputeShaderText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.raytracing_shader",
            .menuPath        = "Shader/Raytracing Shader",
            .displayName     = "Raytracing Shader",
            .defaultFileName = "NewRaytracing.vshader",
            .extension       = ".vshader",
            .assetType       = vasset::VAssetType::eUnknown,
            .openInCodeEditor = true,
            .makeText        = makeRayTracingShaderText,
        });

        registered = true;
    }
} // namespace vultra_app
