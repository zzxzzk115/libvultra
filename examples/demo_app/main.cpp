#include <vultra/core/app/app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input_system.hpp>
#include <vultra/core/os/window_system.hpp>
#include <vultra/core/rhi/framebuffer_info.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/shader_type.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/camera/camera_system.hpp>
#include <vultra/function/debugging/frame_debugger_system.hpp>
#include <vultra/function/imgui/imgui_system.hpp>
#include <vultra/function/rendering/backend/render_backend_system.hpp>
#include <vultra/function/rendering/render_system.hpp>
#include <vultra/function/rendering/shader_system.hpp>
#include <vultra/function/rendering/srp/builtin/universal_renderer.hpp>
#include <vultra/function/rendering/srp/render_context.hpp>
#include <vultra/function/resource/gpu_resource_system.hpp>
#include <vultra/function/scene/scene_system.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world_system.hpp>

using namespace vultra;

struct SimpleVertex
{
    glm::vec3 position;
    glm::vec3 color;
};

// Triangle in NDC for simplicity.
constexpr auto kTriangle = std::array {
    // clang-format off
        //                    position                 color
        SimpleVertex{ {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top
        SimpleVertex{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // left
        SimpleVertex{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }  // right
    // clang-format on
};

class TriangleRenderer : public Renderer
{
public:
    virtual std::string_view name() const override { return "triangle"; }

    virtual void init() override
    {
        auto& rd        = getServices()->require<IRenderBackendService>().renderDevice();
        auto& swapchain = getServices()->require<IRenderBackendService>().swapchain();

        // Create vertex buffer
        m_VertexBuffer = rd.createVertexBuffer(sizeof(SimpleVertex), 3);

        // Upload vertex buffer
        {
            constexpr auto kVerticesSize = sizeof(SimpleVertex) * kTriangle.size();
            rd.uploadS(m_VertexBuffer, 0, kVerticesSize, kTriangle.data());
        }

        const auto* const vertCode = R"(
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out vec3 v_FragColor;

void main() {
  v_FragColor = a_Color;
  gl_Position = vec4(a_Position, 1.0);
  gl_Position.y *= -1.0;
})";
        const auto* const fragCode = R"(
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 v_FragColor;
layout(location = 0) out vec4 FragColor;

void main() {
  FragColor = vec4(v_FragColor, 1.0);
})";

        // Create graphics pipeline
        m_GraphicsPipeline = rhi::GraphicsPipeline::Builder {}
                                 .setColorFormats({swapchain.getPixelFormat()})
                                 .setInputAssembly({
                                     {0, {.type = rhi::VertexAttribute::Type::eFloat3, .offset = 0}},
                                     {1,
                                      {
                                          .type   = rhi::VertexAttribute::Type::eFloat3,
                                          .offset = offsetof(SimpleVertex, color),
                                      }},
                                 })
                                 .addShader(rhi::ShaderType::eVertex, {.code = vertCode})
                                 .addShader(rhi::ShaderType::eFragment, {.code = fragCode})
                                 .setDepthStencil({
                                     .depthTest  = false,
                                     .depthWrite = false,
                                 })
                                 .setRasterizer({.polygonMode = rhi::PolygonMode::eFill})
                                 .setBlending(0, {.enabled = false})
                                 .build(rd);
    }

    virtual void render(ImmediateRenderContext& ctx) override
    {
        auto& backBuffer = *ctx.view().target;
        auto  extent     = ctx.view().extent;
        rhi::prepareForAttachment(ctx.cb, backBuffer, false);
        ctx.cb
            .beginRendering(rhi::FramebufferInfo {
                .area = {.offset = {0, 0}, .extent = extent},
                .colorAttachments =
                    {
                        rhi::AttachmentInfo {.target = &backBuffer, .clearValue = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f)},
                    },
            })
            .bindPipeline(m_GraphicsPipeline)
            .draw({
                .vertexBuffer = &m_VertexBuffer,
                .numVertices  = static_cast<uint32_t>(kTriangle.size()),
            })
            .endRendering();
    }

private:
    rhi::VertexBuffer     m_VertexBuffer;
    rhi::GraphicsPipeline m_GraphicsPipeline;
};

class DemoAppHost : public AppHost
{
protected:
    void onConfigure(Engine& engine) override
    {
        engine.ctx().config.window.title     = "Vultra Demo App";
        engine.ctx().config.window.resizable = false;

        engine.emplaceSubsystem<WindowSystem>();
        engine.emplaceSubsystem<InputSystem>();

        auto triangleRenderer  = createRef<TriangleRenderer>();
        auto universalRenderer = createRef<UniversalRenderer>();

        auto& camSystem = engine.emplaceSubsystem<CameraSystem>();
        // camSystem.addManualCamera({.rendererKey = triangleRenderer->name().data()});
        auto& cam      = camSystem.addManualCamera({.rendererKey = universalRenderer->name().data()});
        cam.view       = glm::lookAt(glm::vec3(0, 1, 1.5), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
        cam.projection = glm::perspective(glm::radians(45.0f),
                                          1.0f * engine.ctx().config.window.width / engine.ctx().config.window.height,
                                          0.1f,
                                          100.0f);
        cam.projection[1][1] *= -1; // Flip Y for Vulkan

        engine.emplaceSubsystem<WorldSystem>();

        engine.emplaceSubsystem<FrameDebuggerSystem>();
        engine.emplaceSubsystem<ShaderSystem>();
        engine.emplaceSubsystem<RenderBackendSystem>();
        engine.emplaceSubsystem<ImGuiSystem>();

        auto& renderSystem = engine.emplaceSubsystem<RenderSystem>();
        renderSystem.registerRenderer(triangleRenderer);
        renderSystem.registerRenderer(universalRenderer);

        engine.emplaceSubsystem<GpuResourceSystem>();
        engine.emplaceSubsystem<AssetSystem>();
        engine.emplaceSubsystem<SceneSystem>();
    }

    void onPostConfigure(Engine& engine) override
    {
        auto& assetService = engine.ctx().services.require<IAssetService>();
        auto  mesh         = assetService.loadMeshSync("res://models/DamagedHelmet/DamagedHelmet.gltf");

        VULTRA_CLIENT_INFO("Loaded mesh with uuid: {}", vbase::to_string(mesh.uuid()));

        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& worldService = engine.ctx().services.require<IWorldService>();

        auto& world = worldService.world();
        auto  root  = sceneService.instantiateScene(world, "res://scenes/test.vscn");

        VULTRA_CLIENT_INFO("Loaded world from scene: \"res://scenes/test.vscn\"");

        auto& registry = world.registry();
        registry.view<NameComponent, TransformComponent>().each(
            [](auto, NameComponent& name, TransformComponent& transform) {
                std::cout << "Entity with transform: " << name.name << "\n";

                std::cout << " Position: " << transform.position.x << ", " << transform.position.y << ", "
                          << transform.position.z << "\n";
                std::cout << " Rotation: " << transform.rotation.x << ", " << transform.rotation.y << ", "
                          << transform.rotation.z << ", " << transform.rotation.w << "\n";
                std::cout << " Scale:    " << transform.scale.x << ", " << transform.scale.y << ", "
                          << transform.scale.z << "\n";
            });

        registry.view<NameComponent, MeshComponent>().each([](auto, NameComponent& name, MeshComponent& mesh) {
            std::cout << "Entity with mesh: " << name.name << "\n";
            std::cout << " Mesh UUID: " << mesh.mesh.toString() << "\n";
        });

        sceneService.saveWorldAsSceneSync("res://scenes/test_saved.vscn", world);
    }

    void onPollEvents() override
    {
        auto& win = engineCtx().services.require<IWindowService>().window();
        win.pollEvents();

        auto& input = engineCtx().services.require<IInputService>();
        if (input.getKeyDown(KeyCode::eEscape))
        {
            win.close();
        }
    }

    bool onShouldClose() const override
    {
        auto& win = engineCtx().services.require<IWindowService>().window();
        return win.shouldClose();
    }
};

int main()
{
    DemoAppHost app {};
    return app.run();
}