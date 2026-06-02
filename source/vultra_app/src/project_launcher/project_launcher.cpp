#include "project_launcher/project_launcher.hpp"

#include "common/ui_widgets.hpp"
#include "project_templates.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace vultra_app
{
    namespace
    {
        std::string sanitizeProjectName(std::string name)
        {
            name.erase(std::remove_if(name.begin(),
                                      name.end(),
                                      [](unsigned char ch) {
                                          return ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                                                 ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*';
                                      }),
                       name.end());

            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
                name.erase(name.begin());
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                name.pop_back();

            return name;
        }

        std::filesystem::path normalizeProjectPath(const std::filesystem::path& path)
        {
            if (path.extension() == ".vproject")
                return path.parent_path().lexically_normal();
            return path.lexically_normal();
        }

        bool sameProjectPath(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return a.lexically_normal().generic_string() == b.lexically_normal().generic_string();
        }

        std::string toLower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        bool projectMatchesSearch(const std::string& query, const std::string& name, const std::filesystem::path& path)
        {
            if (query.empty())
                return true;

            return toLower(name).find(query) != std::string::npos ||
                   toLower(path.generic_string()).find(query) != std::string::npos;
        }

        bool writeTextFile(const std::filesystem::path& path, std::string_view text, std::string& errorMessage)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec)
            {
                errorMessage =
                    "failed to create directory '" + path.parent_path().generic_string() + "': " + ec.message();
                return false;
            }

            std::ofstream file(path, std::ios::trunc);
            if (!file)
            {
                errorMessage = "failed to open '" + path.generic_string() + "' for writing";
                return false;
            }

            file << text;
            if (!file)
            {
                errorMessage = "failed to write '" + path.generic_string() + "'";
                return false;
            }
            return true;
        }

        bool writeDefaultProjectAssets(const std::filesystem::path& projectDir, std::string& errorMessage)
        {
            constexpr std::string_view kSampleScene = R"([vscn]
version = 1
root    = 0

[node id=1 name="Sun" parent=0 uuid="206733c1f880193cbd1dd2b65e0d0ca8"]
NameComponent/name = "Sun"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 4, 0)
TransformComponent/rotation = (-0.699544, -0.111872, 0.112315, 0.696784)
TransformComponent/scale = (1, 1, 1)
LightComponent/kind = 0
LightComponent/color = (1, 0.96, 0.9)
LightComponent/intensity = 8
LightComponent/range = 100
LightComponent/radius = 0.05
LightComponent/width = 1
LightComponent/height = 1
LightComponent/innerConeDegrees = 20
LightComponent/outerConeDegrees = 30
LightComponent/castsShadow = true
LightComponent/twoSided = false

[node id=2 name="Camera" parent=0 uuid="d6348e9e870dff93209ad02615cfefbb"]
NameComponent/name = "Camera"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 1.6, 4)
TransformComponent/rotation = (-0.130526, 2.16687e-08, 2.85274e-09, 0.991445)
TransformComponent/scale = (1, 1, 1)
CameraComponent/primary = true
CameraComponent/projection = 0
CameraComponent/fovYDegrees = 60.000000
CameraComponent/orthographicHeight = 10.000000
CameraComponent/zNear = 0.100000
CameraComponent/zFar = 1000.000000
CameraComponent/clearMode = 1
CameraComponent/clearColor = (0.02, 0.025, 0.035, 1)
CameraComponent/priority = 0
CameraComponent/rendererKey = "universal"

[node id=3 name="Environment" parent=0 uuid="2ed6e38597034df78145da7f6b56d6ad"]
NameComponent/name = "Environment"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 0, 0)
TransformComponent/rotation = (0, 0, 0, 1)
TransformComponent/scale = (1, 1, 1)
EnvironmentComponent/active = true
EnvironmentComponent/skybox = "9a5cf6f3664c0e0b07179f44e84101cc"
EnvironmentComponent/ambientColor = (0.15, 0.15, 0.15)
EnvironmentComponent/ambientIntensity = 1.000000
EnvironmentComponent/enableIBL = false
EnvironmentComponent/iblColor = (0.04, 0.045, 0.05)
EnvironmentComponent/iblIntensity = 1.000000
)";

            constexpr std::string_view kDefaultRenderGraph = R"({
  "version": 3,
  "meta": {
    "editor": {
      "nodes": {
        "DirectDepthPre": { "pos": [-30.0, 80.0] },
        "DirectGBuffer": { "pos": [260.0, 80.0] },
        "Ssao": { "pos": [708.0, -251.0] },
        "ShadowMap": { "pos": [559.0, 366.0] },
        "DeferredLighting": { "pos": [1134.0, -94.0] },
        "GeneralGaussianSplatComposite": { "pos": [1520.0, 315.0] },
        "Ssr": { "pos": [1925.0, 433.0] },
        "SsrComposite": { "pos": [2420.0, -92.0] },
        "Pixelate": { "pos": [2688.0, -69.0] },
        "Invert": { "pos": [3030.0, -21.0] },
        "ToneMapping": { "pos": [3413.0, -42.0] },
        "Fxaa": { "pos": [3796.0, 3.0] },
        "SelectionOutline": { "pos": [4081.0, 176.0] },
        "UiOverlay": { "pos": [4300.0, 176.0] },
        "FinalComposition": { "pos": [4470.0, 208.0] }
      }
    }
  },
  "passes": [    {
      "enabled": true,
      "id": "DirectDepthPre",
      "type": "DirectDepthPre"
    },
    {
      "enabled": true,
      "id": "DirectGBuffer",
      "inputs": {
        "depth": "DirectDepthPre.depth"
      },
      "type": "DirectGBuffer"
    },
    {
      "enabled": true,
      "id": "Ssao",
      "inputs": {
        "depth": "DirectGBuffer.depth",
        "normal": "DirectGBuffer.normal"
      },
      "params": {
        "bias": 0.05,
        "directionCount": 4,
        "enabled": false,
        "intensity": 1.0,
        "maxRadiusPixels": 16,
        "radius": 1.5,
        "stepCount": 2
      },
      "type": "Ssao"
    },
    {
      "enabled": true,
      "id": "ShadowMap",
      "params": {
        "autoFitBounds": true,
        "cascadeCount": 4,
        "coverageRadius": 75.0,
        "depthBias": 0.0012,
        "enabled": true,
        "lightDistance": 120.0,
        "normalBias": 0.015,
        "pcssLightRadius": 1.5,
        "resolution": 2048,
        "splitLambda": 0.6,
        "stableTexelSnapping": true,
        "zRange": 120.0
      },
      "type": "ShadowMap"
    },
    {
      "enabled": true,
      "id": "DeferredLighting",
      "inputs": {
        "ao": "Ssao.ao",
        "color": "DirectGBuffer.color",
        "depth": "DirectGBuffer.depth",
        "material": "DirectGBuffer.material",
        "normal": "DirectGBuffer.normal",
        "shadowData": "ShadowMap.shadowData",
        "shadowMap": "ShadowMap.shadowMap"
      },
      "params": {
        "ambientIntensity": 1.0,
        "debugCascades": false,
        "debugViewMode": 0,
        "iblIntensity": 1.0,
        "pcfRadius": 2,
        "pcssBlockerSamples": 6,
        "shadowDebugMode": 0,
        "shadowFilterMode": 1,
        "shadowStrength": 0.85
      },
      "type": "DeferredLighting"
    },
    {
      "enabled": true,
      "id": "GeneralGaussianSplatComposite",
      "inputs": {
        "source": "DeferredLighting.color"
      },
      "type": "GeneralGaussianSplatComposite"
    },
    {
      "enabled": true,
      "id": "Ssr",
      "inputs": {
        "color": "GeneralGaussianSplatComposite.color",
        "depth": "DirectGBuffer.depth",
        "material": "DirectGBuffer.material",
        "normal": "DirectGBuffer.normal"
      },
      "params": {
        "binaryRefinement": 2,
        "enabled": false,
        "maxSteps": 8,
        "reflectionFactor": 0.2,
        "stride": 0.35,
        "thickness": 0.5
      },
      "type": "Ssr"
    },
    {
      "enabled": true,
      "id": "SsrComposite",
      "inputs": {
        "reflection": "Ssr.reflection",
        "source": "GeneralGaussianSplatComposite.color"
      },
      "params": {
        "enabled": false
      },
      "type": "SsrComposite"
    },
    {
      "enabled": false,
      "id": "Pixelate",
      "inputs": {
        "source": "SsrComposite.color"
      },
      "params": {
        "gridOffset": 0.5,
        "mode": 0,
        "name": "Pixelate",
        "pixelSize": 8.0,
        "preserveAlpha": true
      },
      "type": "Pixelate"
    },
    {
      "enabled": false,
      "id": "Invert",
      "inputs": {
        "source": "Pixelate.color"
      },
      "params": {
        "mode": 0,
        "name": "Invert",
        "preserveAlpha": true,
        "strength": 1.0
      },
      "type": "Invert"
    },
    {
      "enabled": true,
      "id": "ToneMapping",
      "inputs": {
        "source": "Invert.color"
      },
      "params": {
        "enabled": true,
        "exposure": 1.0,
        "method": 0
      },
      "type": "ToneMapping"
    },
    {
      "enabled": true,
      "id": "Fxaa",
      "inputs": {
        "source": "ToneMapping.color"
      },
      "params": {
        "enabled": true
      },
      "type": "Fxaa"
    },
    {
      "enabled": true,
      "id": "SelectionOutline",
      "inputs": {
        "depth": "DirectGBuffer.depth",
        "entityId": "DirectGBuffer.entityId",
        "source": "Fxaa.color"
      },
      "params": {
        "edgeOpacity": 0.35,
        "enabled": true,
        "fillOpacity": 0.0,
        "thickness": 3.0
      },
      "type": "SelectionOutline"
    },
    {
      "enabled": true,
      "id": "UiOverlay",
      "inputs": {
        "source": "SelectionOutline.color"
      },
      "params": {
        "enabled": true
      },
      "type": "UiOverlay"
    },
    {
      "enabled": true,
      "id": "FinalComposition",
      "inputs": {
        "source": "UiOverlay.color"
      },
      "type": "FinalComposition"
    }
  ]
}
)";

            constexpr std::string_view kStereoRenderGraph = R"({
  "version": 3,
  "meta": {
    "editor": {
      "nodes": {
        "DirectDepthPre": { "pos": [-30.0, 100.0] },
        "DirectGBuffer": { "pos": [260.0, 100.0] },
        "ShadowMap": { "pos": [260.0, 460.0] },
        "Ssao": { "pos": [620.0, 260.0] },
        "DeferredLighting": { "pos": [980.0, 160.0] },
        "ToneMapping": { "pos": [1340.0, 160.0] },
        "Fxaa": { "pos": [1700.0, 160.0] },
        "UiOverlay": { "pos": [1880.0, 160.0] },
        "FinalComposition": { "pos": [2060.0, 160.0] }
      }
    }
  },
  "passes": [    {
      "enabled": true,
      "id": "DirectDepthPre",
      "type": "DirectDepthPre",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "DirectGBuffer",
      "inputs": {
        "depth": "DirectDepthPre.depth"
      },
      "type": "DirectGBuffer",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "ShadowMap",
      "type": "ShadowMap",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "Ssao",
      "inputs": {
        "depth": "DirectGBuffer.depth",
        "normal": "DirectGBuffer.normal"
      },
      "params": {
        "enabled": false,
        "maxRadiusPixels": 16,
        "stepCount": 2,
        "directionCount": 4
      },
      "type": "Ssao",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "DeferredLighting",
      "inputs": {
        "color": "DirectGBuffer.color",
        "normal": "DirectGBuffer.normal",
        "material": "DirectGBuffer.material",
        "depth": "DirectGBuffer.depth",
        "ao": "Ssao.ao",
        "shadowMap": "ShadowMap.shadowMap",
        "shadowData": "ShadowMap.shadowData"
      },
      "type": "DeferredLighting",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "ToneMapping",
      "inputs": {
        "source": "DeferredLighting.color"
      },
      "params": {
        "enabled": true
      },
      "type": "ToneMapping",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "Fxaa",
      "inputs": {
        "source": "ToneMapping.color"
      },
      "params": {
        "enabled": true
      },
      "type": "Fxaa",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "UiOverlay",
      "inputs": {
        "source": "Fxaa.color"
      },
      "params": {
        "enabled": true
      },
      "type": "UiOverlay",
      "viewMode": "inherit"
    },
    {
      "enabled": true,
      "id": "FinalComposition",
      "inputs": {
        "source": "UiOverlay.color"
      },
      "type": "FinalComposition",
      "viewMode": "inherit"
    }
  ],
  "resources": [
    { "name": "stereo_color" },
    { "name": "stereo_depth" },
    { "name": "previous_stereo_color" },
    { "name": "previous_stereo_depth" },
    { "name": "previous_stereo_pose" },
    { "name": "stereo_reprojection_metadata" }
  ]
}
)";

            constexpr std::string_view kPixelatePass = R"(return RenderGraphPass {
    type = "Pixelate",
    shader = {
        library = "project",
        vertexLibrary = "builtin",
        vertex = "fullscreen_triangle.vert",
        fragment = "pixelate.frag",
    },
}
)";

            constexpr std::string_view kInvertPass = R"(return RenderGraphPass {
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
)";

constexpr std::string_view kShaderLibrary = R"(return ShaderLibrary {
    name = "project",
    root = "shaders",
    shaders = {
        "**/*.vshader",
    },
}
)";

            constexpr std::string_view kPixelateShader = R"([vshader]
language = glsl
version = 460

[frag]
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

layout (set = 3, binding = 0) uniform sampler2D t_0;

void main() {
    const float pixelSize = 8.0;
    vec2 sourceSize = vec2(textureSize(t_0, 0));
    vec2 pixel = floor(v_TexCoord * sourceSize / pixelSize) * pixelSize + vec2(0.5 * pixelSize);
    vec2 uv = clamp(pixel / sourceSize, vec2(0.0), vec2(1.0));
    FragColor = texture(t_0, uv);
}
)";

            constexpr std::string_view kComputeInvertShader = R"([vshader]
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

    vec4 color = texelFetch(u_Source, pixel, 0);
    imageStore(u_Output, pixel, vec4(vec3(1.0) - color.rgb, color.a));
}
)";

            constexpr std::string_view kDefaultMaterialGraph = R"({
  "version": 1,
  "domain": "surface",
  "name": "Default Material Graph",
  "metadata": {
    "description": "Default surface material graph for new projects."
  },
  "nodes": [
    {
      "id": "BaseColor",
      "type": "vultra.param.color",
      "displayName": "Base Color",
      "params": {
        "value": [0.8, 0.82, 0.86, 1.0]
      },
      "inputs": [],
      "outputs": [
        { "name": "value", "type": "color" }
      ],
      "editor": { "pos": [120.0, 120.0] }
    },
    {
      "id": "Roughness",
      "type": "vultra.param.float",
      "displayName": "Roughness",
      "params": {
        "value": 0.55
      },
      "inputs": [],
      "outputs": [
        { "name": "value", "type": "float" }
      ],
      "editor": { "pos": [120.0, 300.0] }
    },
    {
      "id": "Surface",
      "type": "vultra.output.surface",
      "displayName": "Surface Output",
      "params": {
        "baseColor": [1.0, 1.0, 1.0, 1.0],
        "metallic": 0.0,
        "roughness": 1.0,
        "ao": 1.0,
        "emissive": [0.0, 0.0, 0.0],
        "alpha": 1.0,
        "alphaCutoff": 0.5,
        "alphaMode": "Opaque",
        "shadingModel": "PBRMetallicRoughness"
      },
      "inputs": [
        { "name": "baseColor", "type": "color", "default": [1.0, 1.0, 1.0, 1.0] },
        { "name": "normal", "type": "vec3" },
        { "name": "metallic", "type": "float", "default": 0.0 },
        { "name": "roughness", "type": "float", "default": 1.0 },
        { "name": "ao", "type": "float", "default": 1.0 },
        { "name": "emissive", "type": "vec3", "default": [0.0, 0.0, 0.0] },
        { "name": "alpha", "type": "float", "default": 1.0 },
        { "name": "alphaCutoff", "type": "float", "default": 0.5 }
      ],
      "outputs": [],
      "editor": { "pos": [520.0, 200.0] }
    }
  ],
  "links": [
    {
      "from": { "node": "BaseColor", "pin": "value" },
      "to": { "node": "Surface", "pin": "baseColor" }
    },
    {
      "from": { "node": "Roughness", "pin": "value" },
      "to": { "node": "Surface", "pin": "roughness" }
    }
  ]
}
)";

            constexpr std::string_view kProjectAiReadme = R"(# Vultra Project AI Workspace

This directory is the tracked AI collaboration layer for this Vultra game
project. Keep game-specific intent here, not in the engine repository Harness.

## Directories

- `game.md` stores the game brief and creative constraints.
- `specs/` stores feature, content, level, UI, and rendering specs.
- `tasks/` stores task-centered creation plans.
- `workspace/` stores journals, handoff notes, playtest notes, and verification logs.
- `knowledge/` stores project-specific facts such as naming, lore, resource paths, and conventions.
- `agents/` stores project agent roles and handoff rules.
- `generated/` indexes AI-generated content and provenance records.

Project agents may edit project content through controlled tools, but they must
not directly edit Vultra engine source. If a game task needs new engine behavior,
create an engine task in the Vultra engine Harness.
)";

            constexpr std::string_view kProjectGameBrief = R"(# Game Brief

## Identity

- Title: Untitled Vultra Project
- Genre:
- Camera / View:
- Target Platform:

## Core Loop

Describe the repeatable player loop here.

## Creative Direction

- Visual Style:
- Audio Mood:
- Reference Constraints:

## Technical Boundaries

- Default Scene: `res://scenes/main.vscn`
- Editing Render Graph: `res://render/default.vrg.json`
- Runtime Asset Root: `resources`

## Current Priorities

1. Establish the first playable scene.
2. Define the first gameplay system spec.
3. Record playtest notes in `ai/workspace/`.
)";

            constexpr std::string_view kProjectSpecsReadme = R"(# Project Specs

Use this directory for game-specific specs such as gameplay systems, levels,
characters, UI flows, camera behavior, and render style.

Specs should define intent, constraints, content touchpoints, and acceptance
criteria before tasks start.
)";

            constexpr std::string_view kProjectTasksReadme = R"(# Project Tasks

Use this directory for task-centered game creation work.

Each task should include the linked spec, project files to touch, dry-run patch
expectations, verification steps, and handoff notes.
)";

            constexpr std::string_view kProjectWorkspaceReadme = R"(# Project Workspace

Use this directory for journals, handoffs, playtest notes, verification logs,
and short-lived planning artifacts.
)";

            constexpr std::string_view kProjectKnowledgeReadme = R"(# Project Knowledge

Use this directory for stable game-specific facts: naming conventions, resource
paths, story rules, character definitions, and project-only workflow decisions.
)";

            constexpr std::string_view kProjectAgentsReadme = R"(# Project Agent Roles

Suggested project roles:

- `designer`: turns creative goals into specs and tasks.
- `level-builder`: edits scenes and level content.
- `script-writer`: edits gameplay scripts.
- `asset-curator`: plans imports and tracks generated assets.
- `render-tuner`: adjusts render graphs, shaders, and visual settings.

Agents should hand off through project tasks and workspace journals.
)";

            constexpr std::string_view kProjectGeneratedReadme = R"(# Generated Content Index

Use this directory to record AI-generated content, source prompts, provenance,
review status, and where accepted assets were placed under `resources/`.

This directory is an index, not the runtime asset root.
)";

            const auto resourcesDir = projectDir / "resources";
            return writeTextFile(resourcesDir / "scenes" / "main.vscn", kSampleScene, errorMessage) &&
                   writeTextFile(resourcesDir / "render" / "default.vrg.json", kDefaultRenderGraph, errorMessage) &&
                   writeTextFile(resourcesDir / "render" / "stereo_vr.vrg.json", kStereoRenderGraph, errorMessage) &&
                   writeTextFile(resourcesDir / "render" / "passes" / "pixelate.lua", kPixelatePass, errorMessage) &&
                   writeTextFile(resourcesDir / "render" / "passes" / "invert.lua", kInvertPass, errorMessage) &&
                   writeTextFile(
                       resourcesDir / "materials" / "default.vmatgraph.json", kDefaultMaterialGraph, errorMessage) &&
                   writeTextFile(resourcesDir / "shaders" / "project.vshaderlib.lua", kShaderLibrary, errorMessage) &&
                   writeTextFile(resourcesDir / "shaders" / "fullscreen" / "pixelate.frag.vshader",
                                 kPixelateShader,
                                 errorMessage) &&
                   writeTextFile(resourcesDir / "shaders" / "compute" / "invert.comp.vshader",
                                 kComputeInvertShader,
                                 errorMessage) &&
                   writeTextFile(projectDir / "ai" / "README.md", kProjectAiReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "game.md", kProjectGameBrief, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "specs" / "README.md", kProjectSpecsReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "tasks" / "README.md", kProjectTasksReadme, errorMessage) &&
                   writeTextFile(
                       projectDir / "ai" / "workspace" / "README.md", kProjectWorkspaceReadme, errorMessage) &&
                   writeTextFile(
                       projectDir / "ai" / "knowledge" / "README.md", kProjectKnowledgeReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "agents" / "README.md", kProjectAgentsReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "generated" / "README.md", kProjectGeneratedReadme, errorMessage);
        }

        void drawLauncherLogo(ImDrawList* drawList, ImVec2 center)
        {
            namespace theme = vultra::imgui_theme;
            drawList->AddCircleFilled(center, 24.0f, theme::u32(theme::backgroundDeep()), 48);
            drawList->AddCircle(center, 24.0f, theme::u32(theme::accent()), 48, 1.7f);
            drawList->AddText(ImVec2(center.x - 7.0f, center.y - 11.0f), theme::u32(theme::text()), "V");
        }

        void setTooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
        }

        bool windowControlButton(const char* label, const char* tooltip, bool destructive = false)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {9.0f, 4.0f});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  destructive ? vultra::imgui_theme::destructiveHovered() :
                                                vultra::imgui_theme::buttonHovered());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  destructive ? vultra::imgui_theme::destructiveActive() :
                                                vultra::imgui_theme::accentButton());
            const bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            setTooltip(tooltip);
            return pressed;
        }

        void drawWindowControls(IWindowService* windowService, const ImVec2 origin, const ImVec2 size)
        {
            if (!windowService || windowService->window().isDecorated())
                return;

            ImGui::SetCursorScreenPos(ImVec2 {origin.x + size.x - 116.0f, origin.y + 18.0f});
            if (windowControlButton(ICON_MDI_WINDOW_MINIMIZE, "Minimize"))
                windowService->window().minimize();
            ImGui::SameLine(0.0f, 0.0f);

            const bool maximized = windowService->window().isFullscreen() || windowService->window().isMaximized();
            if (windowControlButton(maximized ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE,
                                    maximized ? "Restore" : "Maximize"))
            {
                if (maximized)
                {
                    if (windowService->window().isFullscreen())
                        windowService->window().setFullscreen(false);
                    else
                        windowService->window().restore();
                }
                else
                {
                    windowService->window().setFullscreen(true);
                }
            }
            ImGui::SameLine(0.0f, 0.0f);

            if (windowControlButton(ICON_MDI_CLOSE, "Close", true))
                windowService->window().close();
        }

        void resetWindowModeForLauncher(IWindowService& windowService)
        {
            auto& window = windowService.window();
            if (window.isFullscreen())
                window.setFullscreen(false);
            if (window.isMaximized())
                window.restore();
        }

        bool drawSidebarButton(const char* id, const char* icon, const char* label, bool active, ImVec2 size)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImGui::InvisibleButton(id, size);

            const bool   clicked = ImGui::IsItemClicked();
            const bool   hovered = ImGui::IsItemHovered();
            const ImVec2 min     = ImGui::GetItemRectMin();
            const ImVec2 max     = ImGui::GetItemRectMax();

            if (active || hovered)
            {
                auto activeFill  = vultra::imgui_theme::frameActive();
                activeFill.w     = 0.90f;
                auto hoverFill   = vultra::imgui_theme::frame();
                hoverFill.w      = 0.82f;
                const ImU32 fill = active ? vultra::imgui_theme::u32(activeFill) : vultra::imgui_theme::u32(hoverFill);
                drawList->AddRectFilled(min, max, fill, 7.0f);
            }

            const ImU32 iconColor =
                vultra::imgui_theme::u32(active ? vultra::imgui_theme::accent() : vultra::imgui_theme::textMuted());
            const ImU32 textColor =
                vultra::imgui_theme::u32(active ? vultra::imgui_theme::text() : vultra::imgui_theme::textMuted());
            drawList->AddText(ImVec2(min.x + 18.0f, min.y + 13.0f), iconColor, icon);
            drawList->AddText(ImVec2(min.x + 48.0f, min.y + 13.0f), textColor, label);
            return clicked;
        }
    } // namespace

    void ProjectLauncher::configureAssets(vultra::Engine& engine, const LaunchOptions& options)
    {
        engine.ctx().config.asset.loadFromVPK = false;
        if (!options.projectPath.empty())
        {
            if (auto project = loadVProject(options.projectPath); project.has_value())
            {
                engine.ctx().config.asset.assetRoot =
                    (project->projectDir / project->assetRoot).lexically_normal().generic_string();
                engine.ctx().config.render.renderPipelineAsset = project->editingRenderGraph;
            }
            else
                engine.ctx().config.asset.assetRoot =
                    (std::filesystem::path(options.projectPath) / "resources").lexically_normal().generic_string();
            return;
        }

        const std::filesystem::path launcherAssetRoot {".vultra_launcher_resources"};
        std::error_code             ec;
        std::filesystem::create_directories(launcherAssetRoot, ec);
        engine.ctx().config.asset.assetRoot = launcherAssetRoot.generic_string();
    }

    void ProjectLauncher::logStartup()
    {
        VULTRA_CLIENT_INFO("[Vultra] No default VPK found. Project Launcher mode is active.");
    }

    void ProjectLauncher::draw(AppState& state, IWindowService* windowService)
    {
        namespace theme = vultra::imgui_theme;

        if (!m_HasScannedProjects)
            loadKnownProjects(state);

        if (windowService)
        {
            auto& window = windowService->window();
            if (window.getTitle() != kWindowTitle)
            {
                resetWindowModeForLauncher(*windowService);
                window.setTitle(kWindowTitle)
                    .setDecorated(false)
                    .setResizable(true)
                    .setExtent({1280, 720})
                    .centerOnScreen()
                    .setVisible(true);
            }
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, vultra::imgui_theme::background());
        ImGui::Begin("##VultraProjectLauncher",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);

        ImDrawList*  drawList = ImGui::GetWindowDrawList();
        const ImVec2 origin   = ImGui::GetWindowPos();
        const ImVec2 size     = ImGui::GetWindowSize();

        const float sidebarW = 276.0f;
        drawList->AddRectFilled(origin,
                                ImVec2(origin.x + size.x, origin.y + size.y),
                                vultra::imgui_theme::u32(vultra::imgui_theme::background()));
        drawList->AddRectFilled(origin,
                                ImVec2(origin.x + sidebarW, origin.y + size.y),
                                vultra::imgui_theme::u32(vultra::imgui_theme::backgroundDeep()));
        drawList->AddLine(ImVec2(origin.x + sidebarW, origin.y + 24.0f),
                          ImVec2(origin.x + sidebarW, origin.y + size.y - 24.0f),
                          theme::u32(theme::withAlpha(theme::border(), 190.0f / 255.0f)),
                          1.0f);

        drawLauncherLogo(drawList, ImVec2(origin.x + 64.0f, origin.y + 78.0f));
        drawList->AddText(ImVec2(origin.x + 104.0f, origin.y + 58.0f),
                          vultra::imgui_theme::u32(vultra::imgui_theme::text()),
                          "Vultra");
        drawList->AddText(
            ImVec2(origin.x + 104.0f, origin.y + 82.0f), theme::u32(theme::textMuted()), "Project Launcher");
        drawWindowControls(windowService, origin, size);

        ImGui::SetCursorScreenPos(ImVec2(origin.x + 28.0f, origin.y + 156.0f));
        drawSidebarButton("##launcher_nav_projects", ICON_MDI_FOLDER_OUTLINE, "Projects", true, ImVec2(220.0f, 48.0f));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 28.0f, origin.y + 214.0f));
        if (drawSidebarButton(
                "##launcher_nav_new", ICON_MDI_PLUS_CIRCLE_OUTLINE, "New Project", false, ImVec2(220.0f, 48.0f)))
            ImGui::OpenPopup("Create Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 28.0f, origin.y + 272.0f));
        if (drawSidebarButton(
                "##launcher_nav_open", ICON_MDI_FOLDER_OPEN_OUTLINE, "Open Existing", false, ImVec2(220.0f, 48.0f)))
            ImGui::OpenPopup("Add Existing Vultra Project");
        const float contentX = origin.x + sidebarW + 40.0f;
        const float contentW = std::max(420.0f, size.x - sidebarW - 80.0f);

        drawList->AddText(ImVec2(contentX, origin.y + 52.0f), theme::u32(theme::text()), "Projects");
        drawList->AddText(ImVec2(contentX, origin.y + 80.0f),
                          theme::u32(theme::textMuted()),
                          "Choose a workspace or create a new one.");

        ImGui::SetCursorScreenPos(ImVec2(contentX, origin.y + 122.0f));
        ImGui::SetNextItemWidth(std::min(460.0f, contentW - 320.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(38.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, vultra::imgui_theme::frame());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, vultra::imgui_theme::frameHovered());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, vultra::imgui_theme::frameActive());
        ImGui::InputTextWithHint("##project_search",
                                 ICON_MDI_MAGNIFY "  Search projects...",
                                 m_SearchQuery.data(),
                                 m_SearchQuery.size(),
                                 ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        const float buttonY = origin.y + 122.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, vultra::imgui_theme::button());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vultra::imgui_theme::buttonHovered());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, vultra::imgui_theme::accentButton());
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 338.0f, buttonY));
        if (ImGui::Button(ICON_MDI_FOLDER_PLUS_OUTLINE "  Add Existing", ImVec2(142.0f, 42.0f)))
            ImGui::OpenPopup("Add Existing Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 184.0f, buttonY));
        ImGui::PushStyleColor(ImGuiCol_Button, vultra::imgui_theme::accentButton());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vultra::imgui_theme::accentButtonHovered());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, vultra::imgui_theme::accentButtonActive());
        if (ImGui::Button(ICON_MDI_PLUS "  New Project", ImVec2(144.0f, 42.0f)))
            ImGui::OpenPopup("Create Vultra Project");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        drawList->AddLine(ImVec2(contentX, origin.y + 178.0f),
                          ImVec2(origin.x + size.x - 40.0f, origin.y + 178.0f),
                          theme::u32(theme::withAlpha(theme::border(), 190.0f / 255.0f)),
                          1.0f);
        drawList->AddText(ImVec2(contentX, origin.y + 206.0f), theme::u32(theme::text()), "Recent Projects");

        const std::string query        = toLower(m_SearchQuery.data());
        float             rowY         = origin.y + 244.0f;
        int               visibleCount = 0;
        const float       rowH         = 82.0f;
        const float       rowGap       = 10.0f;
        const float       listBottom   = origin.y + size.y - 94.0f;

        for (int i = 0; i < static_cast<int>(m_Projects.size()); ++i)
        {
            const auto& project = m_Projects[static_cast<size_t>(i)];
            if (!projectMatchesSearch(query, project.name, project.path))
                continue;
            if (rowY + rowH > listBottom)
                break;

            ++visibleCount;
            const bool   selected = i == m_SelectedProject;
            const ImVec2 rowMin(contentX, rowY);
            const ImVec2 rowMax(origin.x + size.x - 40.0f, rowY + rowH);

            ImGui::SetCursorScreenPos(rowMin);
            ImGui::InvisibleButton(("##project_row_" + std::to_string(i)).c_str(), ImVec2(rowMax.x - rowMin.x, rowH));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                m_SelectedProject = i;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_SelectedProject = i;
                openSelectedProject(state);
            }

            const ImU32 rowFill   = selected ? theme::u32(theme::withAlpha(theme::frameActive(), 245.0f / 255.0f)) :
                                    hovered  ? theme::u32(theme::withAlpha(theme::frameHovered(), 235.0f / 255.0f)) :
                                               theme::u32(theme::withAlpha(theme::frame(), 220.0f / 255.0f));
            const ImU32 rowBorder = selected ? theme::u32(theme::accentTransparent(210.0f / 255.0f)) :
                                               theme::u32(theme::withAlpha(theme::border(), 170.0f / 255.0f));
            drawList->AddRectFilled(rowMin, rowMax, rowFill, 7.0f);
            drawList->AddRect(rowMin, rowMax, rowBorder, 7.0f);

            const ImVec2 tileMin(rowMin.x + 14.0f, rowMin.y + 12.0f);
            const ImVec2 tileMax(tileMin.x + 58.0f, tileMin.y + 58.0f);
            drawList->AddRectFilled(tileMin, tileMax, theme::u32(theme::backgroundDeep()), 6.0f);
            drawList->AddRect(tileMin, tileMax, theme::u32(theme::accentTransparent(190.0f / 255.0f)), 6.0f);
            drawList->AddText(ImVec2(tileMin.x + 19.0f, tileMin.y + 17.0f), theme::u32(theme::accent()), "V");

            drawList->PushClipRect(ImVec2(rowMin.x + 90.0f, rowMin.y), ImVec2(rowMax.x - 190.0f, rowMax.y), true);
            drawList->AddText(
                ImVec2(rowMin.x + 92.0f, rowMin.y + 20.0f), theme::u32(theme::text()), project.name.c_str());
            drawList->AddText(ImVec2(rowMin.x + 92.0f, rowMin.y + 46.0f),
                              theme::u32(theme::textMuted()),
                              project.path.generic_string().c_str());
            drawList->PopClipRect();

            drawList->AddText(ImVec2(rowMax.x - 166.0f, rowMin.y + 22.0f), theme::u32(theme::textSoft()), ".vproject");
            drawList->AddText(ImVec2(rowMax.x - 166.0f, rowMin.y + 48.0f), theme::u32(theme::textMuted()), "Workspace");

            rowY += rowH + rowGap;
        }

        if (visibleCount == 0)
        {
            drawList->AddRectFilled(ImVec2(contentX, origin.y + 244.0f),
                                    ImVec2(origin.x + size.x - 40.0f, origin.y + 338.0f),
                                    theme::u32(theme::withAlpha(theme::frame(), 180.0f / 255.0f)),
                                    7.0f);
            drawList->AddRect(ImVec2(contentX, origin.y + 244.0f),
                              ImVec2(origin.x + size.x - 40.0f, origin.y + 338.0f),
                              theme::u32(theme::withAlpha(theme::border(), 150.0f / 255.0f)),
                              7.0f);
            drawList->AddText(
                ImVec2(contentX + 24.0f, origin.y + 274.0f), theme::u32(theme::text()), "No projects found");
            drawList->AddText(ImVec2(contentX + 24.0f, origin.y + 300.0f),
                              theme::u32(theme::textMuted()),
                              "Create a project or add an existing workspace.");
        }

        const bool hasSelection = m_SelectedProject >= 0 && m_SelectedProject < static_cast<int>(m_Projects.size());

        drawList->AddLine(ImVec2(contentX, origin.y + size.y - 72.0f),
                          ImVec2(origin.x + size.x - 40.0f, origin.y + size.y - 72.0f),
                          theme::u32(theme::withAlpha(theme::border(), 190.0f / 255.0f)),
                          1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 9.0f));
        ImGui::SetCursorScreenPos(ImVec2(contentX, origin.y + size.y - 52.0f));
        if (ImGui::Button(ICON_MDI_IMPORT "  Import Project", ImVec2(142.0f, 38.0f)))
            ImGui::OpenPopup("Add Existing Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 378.0f, origin.y + size.y - 52.0f));
        if (!hasSelection)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_MDI_FOLDER_OPEN "  Open", ImVec2(110.0f, 38.0f)))
            openSelectedProject(state);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 256.0f, origin.y + size.y - 52.0f));
        if (ImGui::Button(ICON_MDI_CLOSE "  Remove", ImVec2(122.0f, 38.0f)))
            removeSelectedProject(state);
        if (!hasSelection)
            ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 122.0f, origin.y + size.y - 52.0f));
        if (ImGui::Button(ICON_MDI_REFRESH, ImVec2(38.0f, 38.0f)))
            loadKnownProjects(state);
        ImGui::SameLine();
        ImGui::Button(ICON_MDI_VIEW_LIST, ImVec2(38.0f, 38.0f));
        ImGui::PopStyleVar(2);

        drawCreateProjectPopup(state);
        drawAddExistingProjectPopup(state);

        if (!state.statusMessage.empty())
        {
            drawList->PushClipRect(ImVec2(contentX + 156.0f, origin.y + size.y - 54.0f),
                                   ImVec2(origin.x + size.x - 396.0f, origin.y + size.y - 18.0f),
                                   true);
            drawList->AddText(ImVec2(contentX + 160.0f, origin.y + size.y - 40.0f),
                              theme::u32(theme::textMuted()),
                              state.statusMessage.c_str());
            drawList->PopClipRect();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void ProjectLauncher::drawCreateProjectPopup(AppState& state)
    {
        bool                 open = true;
        ui::ScopedPopupStyle popupStyle;
        ImGui::SetNextWindowSizeConstraints(ImVec2 {420.0f, 0.0f}, ImVec2 {620.0f, 520.0f});
        if (!ImGui::BeginPopupModal(
                "Create Vultra Project", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            return;

        ui::sectionTitle(ICON_MDI_FOLDER_PLUS_OUTLINE, "New Project");
        ImGui::TextColored(ImVec4 {0.62f, 0.70f, 0.80f, 1.0f},
                           "Choose an empty folder. The folder name becomes the project name.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        m_ProjectRootDialog.draw("Project Folder", m_NewProjectRoot.data(), m_NewProjectRoot.size());
        const char* templates[] = {"Empty", "Minimal"};
        ImGui::Combo("Template", &m_NewProjectTemplate, templates, IM_ARRAYSIZE(templates));

        ImGui::Spacing();
        ImGui::Separator();
        const float buttonWidth = 96.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x -
                             ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(ICON_MDI_PLUS "  Create", ImVec2 {buttonWidth, 0.0f}))
        {
            if (createProject(state))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2 {buttonWidth, 0.0f}))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void ProjectLauncher::drawAddExistingProjectPopup(AppState& state)
    {
        bool                 open = true;
        ui::ScopedPopupStyle popupStyle;
        ImGui::SetNextWindowSizeConstraints(ImVec2 {420.0f, 0.0f}, ImVec2 {620.0f, 460.0f});
        if (!ImGui::BeginPopupModal("Add Existing Vultra Project",
                                    &open,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            return;

        ui::sectionTitle(ICON_MDI_FOLDER_OPEN, "Existing Project");
        ImGui::TextColored(ImVec4 {0.62f, 0.70f, 0.80f, 1.0f}, "Select a project root containing a .vproject file.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        m_ExistingProjectDialog.draw("Project Root", m_ExistingProjectRoot.data(), m_ExistingProjectRoot.size());

        ImGui::Spacing();
        ImGui::Separator();
        const float buttonWidth = 96.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x -
                             ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(ICON_MDI_PLUS "  Add", ImVec2 {buttonWidth, 0.0f}))
        {
            addExistingProject(state);
            if (state.statusMessage.rfind("Added existing project:", 0) == 0)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2 {buttonWidth, 0.0f}))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void ProjectLauncher::loadKnownProjects(AppState& state)
    {
        namespace fs = std::filesystem;

        m_Projects.clear();
        m_SelectedProject = -1;

        std::error_code ec;
        fs::create_directories(state.launcherStateFile.parent_path(), ec);

        std::ifstream file(state.launcherStateFile);
        std::string   line;
        while (std::getline(file, line))
        {
            const auto projectPath = normalizeProjectPath(line);
            if (projectPath.empty())
                continue;

            addKnownProject(state, projectPath);
        }

        std::sort(m_Projects.begin(), m_Projects.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
            return a.name < b.name;
        });
        m_HasScannedProjects = true;
    }

    void ProjectLauncher::saveKnownProjects(const AppState& state) const
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::create_directories(state.launcherStateFile.parent_path(), ec);

        std::ofstream file(state.launcherStateFile, std::ios::trunc);
        for (const auto& project : m_Projects)
            file << project.path.generic_string() << "\n";
    }

    void ProjectLauncher::addKnownProject(AppState& state, const std::filesystem::path& path)
    {
        const auto projectPath = normalizeProjectPath(path);
        if (projectPath.empty())
            return;

        const auto exists = std::any_of(m_Projects.begin(), m_Projects.end(), [&](const ProjectEntry& entry) {
            return sameProjectPath(entry.path, projectPath);
        });
        if (exists)
            return;

        const auto project = loadVProject(projectPath);
        if (!project.has_value())
            return;

        m_Projects.push_back(ProjectEntry {.path = project->projectDir.lexically_normal(), .name = project->name});
        state.statusMessage = "Added project: " + project->projectDir.generic_string();
    }

    bool ProjectLauncher::createProject(AppState& state)
    {
        namespace fs = std::filesystem;

        const fs::path projectDir = fs::path(m_NewProjectRoot.data()).lexically_normal();
        if (projectDir.empty())
        {
            state.statusMessage = "Project folder is empty.";
            return false;
        }

        std::error_code ec;
        if (!fs::exists(projectDir, ec))
        {
            state.statusMessage = "Project folder does not exist: " + projectDir.generic_string();
            return false;
        }
        if (!fs::is_directory(projectDir, ec))
        {
            state.statusMessage = "Project path is not a folder: " + projectDir.generic_string();
            return false;
        }
        if (!fs::is_empty(projectDir, ec) || ec)
        {
            state.statusMessage = ec ? "Failed to inspect project folder: " + ec.message() :
                                       "Project folder must be empty: " + projectDir.generic_string();
            return false;
        }

        const std::string projectName = sanitizeProjectName(projectDir.filename().generic_string());
        if (projectName.empty())
        {
            state.statusMessage = "Project folder name is not a valid project name.";
            return false;
        }

        const fs::path projectFile = vprojectFileFor(projectDir, projectName);
        if (fs::exists(projectFile, ec))
        {
            state.statusMessage = "Project file already exists: " + projectFile.generic_string();
            return false;
        }

        fs::create_directories(projectDir / "resources" / "scenes", ec);
        if (ec)
        {
            state.statusMessage = "Failed to create project: " + ec.message();
            return false;
        }

        std::string errorMessage;
        const auto  templateKind =
            m_NewProjectTemplate == 0 ? ProjectTemplateKind::Empty : ProjectTemplateKind::Minimal;
        VProject    project {
               .projectDir         = projectDir,
               .name               = projectName,
               .assetRoot          = "resources",
               .defaultScene       = "res://scenes/main.vscn",
               .buildScenes        = {VBuildScene {.index = 0, .uri = "res://scenes/main.vscn", .name = "Main", .enabled = true}},
               .editingRenderGraph = templateKind == ProjectTemplateKind::Empty ?
                                         std::string {} :
                                         std::string {"res://render/default.vrg.json"},
        };
        if (!saveVProject(project, &errorMessage))
        {
            state.statusMessage = "Failed to write .vproject: " + errorMessage;
            return false;
        }
        if (!writeProjectTemplateAssets(projectDir, templateKind, errorMessage))
        {
            state.statusMessage = "Failed to write default project assets: " + errorMessage;
            return false;
        }

        addKnownProject(state, projectDir);
        saveKnownProjects(state);
        state.currentProject = project.projectDir;
        state.selectedSourceAsset.clear();
        state.pendingEditorCommands.clear();
        state.currentProjectName        = project.name;
        state.currentAssetRoot          = project.assetRoot;
        state.currentDefaultScene       = project.defaultScene;
        state.currentBuildScenes        = project.buildScenes;
        state.currentEditingRenderGraph = project.editingRenderGraph;
        state.currentEditingMaterialGraph = "res://materials/default.vmatgraph.json";
        ++state.projectGeneration;
        state.renderGraphOpenRequested = false;
        state.materialGraphOpenRequested = false;
        state.editorPlaying           = false;
        state.editorPaused            = false;
        state.editorStepRequested     = false;
        state.editorShutdownRequested = false;
        state.mode                    = AppMode::Editor;
        state.statusMessage           = "Created project: " + projectDir.generic_string();
        return true;
    }

    void ProjectLauncher::addExistingProject(AppState& state)
    {
        const std::filesystem::path projectPath = normalizeProjectPath(m_ExistingProjectRoot.data());
        if (!loadVProject(projectPath).has_value())
        {
            state.statusMessage = "No .vproject found at: " + projectPath.generic_string();
            return;
        }

        addKnownProject(state, projectPath);
        saveKnownProjects(state);
        state.statusMessage = "Added existing project: " + projectPath.generic_string();
    }

    void ProjectLauncher::removeSelectedProject(AppState& state)
    {
        if (m_SelectedProject < 0 || m_SelectedProject >= static_cast<int>(m_Projects.size()))
            return;

        const auto projectDir = m_Projects[static_cast<size_t>(m_SelectedProject)];

        m_Projects.erase(m_Projects.begin() + m_SelectedProject);
        m_SelectedProject = -1;
        saveKnownProjects(state);
        state.statusMessage = "Removed project from launcher: " + projectDir.path.generic_string();
    }

    void ProjectLauncher::openSelectedProject(AppState& state)
    {
        if (m_SelectedProject < 0 || m_SelectedProject >= static_cast<int>(m_Projects.size()))
            return;

        const auto project = loadVProject(m_Projects[static_cast<size_t>(m_SelectedProject)].path);
        if (!project.has_value())
        {
            state.statusMessage = "Failed to open project.vproject.";
            return;
        }

        state.currentProject = project->projectDir;
        state.selectedSourceAsset.clear();
        state.pendingEditorCommands.clear();
        state.currentProjectName        = project->name;
        state.currentAssetRoot          = project->assetRoot;
        state.currentDefaultScene       = project->defaultScene;
        state.currentBuildScenes        = project->buildScenes;
        state.currentEditingRenderGraph = project->editingRenderGraph;
        state.currentEditingMaterialGraph = "res://materials/default.vmatgraph.json";
        ++state.projectGeneration;
        state.renderGraphOpenRequested = false;
        state.materialGraphOpenRequested = false;
        state.editorPlaying           = false;
        state.editorPaused            = false;
        state.editorStepRequested     = false;
        state.editorShutdownRequested = false;
        state.mode                    = AppMode::Editor;
        state.statusMessage           = "Opened project: " + state.currentProject.generic_string();
    }
} // namespace vultra_app
