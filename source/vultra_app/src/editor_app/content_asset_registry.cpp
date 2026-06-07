#include "editor_app/content_asset_registry.hpp"

#include <algorithm>
#include <cctype>

namespace vultra_app
{
    namespace
    {
        // Every project shader needs an explicit, unique [vshader] id. Derive a
        // sane default from the new asset's stem (assetName already has the
        // .vshader extension stripped) and splice it into the template header;
        // the author can rename it. Ids are deep-namespaced under "project/".
        std::string projectShaderId(std::string_view assetName)
        {
            return "project/" + std::string {assetName};
        }

        std::string withProjectShaderId(std::string text, std::string_view assetName)
        {
            const std::string marker {"[vshader]\n"};
            const auto        pos = text.find(marker);
            if (pos != std::string::npos)
                text.insert(pos + marker.size(), "id = \"" + projectShaderId(assetName) + "\"\n");
            return text;
        }

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

        std::string makeSurfaceShaderText(std::string_view assetName)
        {
            return withProjectShaderId(R"([vshader]
language = glsl
version = 460

[properties]
tint : vec4 = (1.0, 0.2, 0.1, 1.0)
roughness : float = 0.5
albedoTex : Texture2D

[frag]
#include "include/vultra/mesh_material.glsl"

VULTRA_MATERIAL_MAIN(shade)

void shade(in VultraMaterialInput IN, inout VultraMaterialEval OUT)
{
    Material m = VULTRA_MATERIAL();
    vec4 tex = VULTRA_SAMPLE2D(m.albedoTex_index, IN.uv0);
    OUT.baseColor = m.tint * tex;
    OUT.roughness = m.roughness;
}
)",
                                       assetName);
        }

        std::string makePostProcessingShaderText(std::string_view assetName)
        {
            return withProjectShaderId(R"([vshader]
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
)",
                                       assetName);
        }

        std::string makeComputeShaderText(std::string_view assetName)
        {
            return withProjectShaderId(R"([vshader]
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
)",
                                       assetName);
        }

        std::string makeRayTracingShaderText(std::string_view assetName)
        {
            return withProjectShaderId(R"([vshader]
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
)",
                                       assetName);
        }

        // Render/compute/raytracing pass creators: a scripted-pass stub with only
        // the type (from the asset name) and empty setup/execute for the user.
        std::string makeRenderPassText(std::string_view assetName)
        {
            return scriptedPassStubLua(passTypeFromAssetName(assetName));
        }

        std::string makeComputePassText(std::string_view assetName)
        {
            return scriptedPassStubLua(passTypeFromAssetName(assetName));
        }

        std::string makeRayTracingPassText(std::string_view assetName)
        {
            return scriptedPassStubLua(passTypeFromAssetName(assetName));
        }

        // Post-processing pass creator: setup/execute pre-filled (standard op).
        std::string makePostProcessingPassText(std::string_view assetName)
        {
            return scriptedPostProcessPassLua(passTypeFromAssetName(assetName), {});
        }

        std::string makeBuiltinPbrMaterialText(std::string_view)
        {
            return R"({
  "type": "Material",
  "version": 1,
  "name": "New Material",
  "source": {
    "kind": "builtin",
    "id": "builtin/pbr"
  },
  "properties": {
    "baseColor": [1.0, 1.0, 1.0, 1.0],
    "metallic": 0.0,
    "roughness": 0.5
  }
}
)";
        }

        std::string makeSingleShaderMaterialText(std::string_view)
        {
            return R"({
  "type": "Material",
  "version": 1,
  "name": "New Shader Material",
  "source": {
    "kind": "shader",
    "shaderLibrary": "project",
    "id": "project/fullscreen/pixelate.frag"
  },
  "properties": {}
}
)";
        }

        std::string makeAnimatorGraphText(std::string_view)
        {
            return R"({
  "version": 1,
  "name": "New Animator Graph",
  "entry": "New State",
  "parameters": [],
  "anyTransitions": [],
  "states": [
    {
      "name": "New State",
      "animation": "00000000000000000000000000000000",
      "loop": true,
      "speed": 1.0,
      "transitions": [],
      "editor": { "pos": [180.0, 0.0] }
    }
  ],
  "metadata": { "anyStatePos": [0.0, 0.0] }
}
)";
        }

        std::string makeMaterialGraphNodeText(std::string_view)
        {
            return R"({
  "type": "MaterialGraphNode",
  "version": 1,
  "typeId": "project.custom_node",
  "displayName": "Custom Node",
  "inputs": [
    { "name": "value", "type": "float", "defaultValue": 0.0 }
  ],
  "outputs": [
    { "name": "out", "type": "float" }
  ],
  "defaultParams": {
    "value": 0.0
  },
  "implementation": {
    "language": "glsl",
    "outputs": {
      "out": "{{input:value}}"
    }
  }
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
            .id              = "vultra.builtin_pbr_material",
            .menuPath        = "Material/Builtin PBR",
            .displayName     = "Builtin PBR Material",
            .defaultFileName = "NewMaterial.vmat.json",
            .extension       = ".vmat.json",
            .assetType       = vasset::VAssetType::eUnknown,
            .openInCodeEditor = true,
            .makeText        = makeBuiltinPbrMaterialText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.single_shader_material",
            .menuPath        = "Material/Single Shader",
            .displayName     = "Single Shader Material",
            .defaultFileName = "NewShaderMaterial.vmat.json",
            .extension       = ".vmat.json",
            .assetType       = vasset::VAssetType::eUnknown,
            .openInCodeEditor = true,
            .makeText        = makeSingleShaderMaterialText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id               = "vultra.animator_graph",
            .menuPath         = "Animation/Animator Graph",
            .displayName      = "Animator Graph",
            .defaultFileName  = "NewAnimatorGraph.vanimgraph.json",
            .extension        = ".vanimgraph.json",
            .assetType        = vasset::VAssetType::eAnimatorGraphJson,
            .openInCodeEditor = false,
            .makeText         = makeAnimatorGraphText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.material_graph_node",
            .menuPath        = "Material Graph/Custom Node",
            .displayName     = "Material Graph Custom Node",
            .defaultFileName = "NewMaterialNode.vmatnode.json",
            .extension       = ".vmatnode.json",
            .assetType       = vasset::VAssetType::eUnknown,
            .openInCodeEditor = true,
            .makeText        = makeMaterialGraphNodeText,
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
            .makeText        = makePostProcessingPassText,
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

    std::string passTypeFromAssetName(std::string_view assetName)
    {
        std::string type;
        type.reserve(assetName.size());
        for (const char c : assetName)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_')
                type.push_back(c);
            else if (c == ' ' || c == '-' || c == '.')
                type.push_back('_');
        }
        while (!type.empty() && type.front() == '_')
            type.erase(type.begin());
        if (type.empty())
            type = "NewPass";
        return type;
    }

    namespace
    {
        std::string replaceAllTokens(std::string text, std::string_view token, std::string_view value)
        {
            for (auto pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + value.size()))
                text.replace(pos, token.size(), value);
            return text;
        }
    } // namespace

    std::string scriptedPostProcessPassLua(std::string_view type, std::string_view fragment)
    {
        const std::string frag = fragment.empty() ? std::string {"TODO_replace_with_your.frag"} : std::string {fragment};
        static constexpr std::string_view kTemplate = R"(-- Post-processing scripted render pass (the project pass standard).
-- setup declares FrameGraph I/O and picks the shader; execute records the
-- fullscreen draw. See doc/scripted_render_passes.md.
local state = {}

return RenderGraphPass {
    type     = "__TYPE__",
    -- Where this pass appears in the render graph "Add" menu ('/'-nested submenus).
    menuPath = "Post Processing/__TYPE__",
    inputs   = { "source" },
    outputs  = { "color" },

    -- Auto-exposes this fragment shader's reflected params on the graph node.
    shader = { fragmentLibrary = "project", fragment = "__FRAG__" },

    setup = function(ctx)
        local src = ctx:getInput("source")
        ctx:read(src, { set = 3, binding = 0, stage = "fragment" })
        local out = ctx:createColorTexture { name = "__TYPE__ Color", inherit = src }
        ctx:writeColor(out)
        ctx:setOutput("color", out)
        ctx:useGraphicsShader {
            vertexLibrary   = "builtin",
            vertex          = "builtin/general/fullscreen_triangle.vert",
            fragmentLibrary = "project",
            fragment        = "__FRAG__",
        }
        -- Read graph params here, e.g. state.strength = ctx:paramFloat("strength", 1.0)
    end,

    execute = function(rc)
        if not rc:bindPipeline() then return end
        rc:bindDescriptorSets()
        -- rc:pushConstants("fragment", { strength = state.strength })
        rc:beginRendering()
        rc:drawFullscreen()
        rc:endRendering()
    end,
}
)";
        return replaceAllTokens(replaceAllTokens(std::string {kTemplate}, "__TYPE__", type), "__FRAG__", frag);
    }

    std::string scriptedPassStubLua(std::string_view type)
    {
        static constexpr std::string_view kTemplate = R"(-- Scripted render pass (the project pass standard).
-- Fill in setup(ctx) and execute(rc) to drive the FrameGraph + command
-- recorder. See doc/scripted_render_passes.md for the full API.
return RenderGraphPass {
    type     = "__TYPE__",
    -- Where this pass appears in the render graph "Add" menu ('/'-nested submenus).
    menuPath = "Custom/__TYPE__",
    inputs   = { "source" },
    outputs  = { "color" },

    setup = function(ctx)
        -- local src = ctx:getInput("source")
        -- ctx:read(src, { set = 3, binding = 0, stage = "fragment" })
        -- local out = ctx:createColorTexture { name = "__TYPE__ Color", inherit = src }
        -- ctx:writeColor(out); ctx:setOutput("color", out)
        -- ctx:useGraphicsShader { ... }  or  ctx:useComputeShader { ... }
    end,

    execute = function(rc)
        -- if not rc:bindPipeline() then return end
        -- rc:bindDescriptorSets()
        -- rc:beginRendering(); rc:drawFullscreen(); rc:endRendering()  -- or rc:dispatch(x, y, z)
    end,
}
)";
        return replaceAllTokens(std::string {kTemplate}, "__TYPE__", type);
    }
} // namespace vultra_app
