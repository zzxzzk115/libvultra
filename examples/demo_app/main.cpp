#include <vultra/core/app/app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input_system.hpp>
#include <vultra/core/os/window_system.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/shader_type.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/camera/camera_system.hpp>
#include <vultra/function/rendering/backend/render_backend_system.hpp>
#include <vultra/function/rendering/render_system.hpp>
#include <vultra/function/rendering/shader/shader_system.hpp>
#include <vultra/function/rendering/srp/render_context.hpp>
#include <vultra/function/resource/gpu_resource_system.hpp>
#include <vultra/function/scene/scene_system.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
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

    virtual void init(RendererServices& services) override
    {
        auto& rd        = services.backendService.renderDevice();
        auto& swapchain = services.backendService.swapchain();

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

    virtual void render(RenderContext& ctx) override
    {
        auto& backBuffer = *ctx.framebufferInfo.value().colorAttachments[0].target;
        rhi::prepareForAttachment(ctx.cb, backBuffer, false);
        ctx.cb.beginRendering(ctx.framebufferInfo.value())
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

class BaseColorRenderer : public Renderer
{
public:
    virtual std::string_view name() const override { return "base_color"; }

    virtual void init(RendererServices& services) override
    {
        auto& rd        = services.backendService.renderDevice();
        auto& swapchain = services.backendService.swapchain();

        // Retrieve the shader from the built-in shader library.
        auto shaderLib         = services.shaderService.bulitinLibrary();
        auto vertexVariantHash = shaderLib.computeVariantHash("mesh.vert",
                                                              vshadersystem::ShaderStage::eVert,
                                                              {
                                                                  {"VTX_HAS_COLOR", 1},
                                                                  {"VTX_HAS_NORMAL", 1},
                                                                  {"VTX_HAS_UV0", 1},
                                                                  {"VTX_HAS_UV1", 1},
                                                                  {"VTX_HAS_TANGENT", 1},
                                                              });
        auto vertexShader      = shaderLib.load(vertexVariantHash, vshadersystem::ShaderStage::eVert);

        auto fragmentVariantHash = shaderLib.computeVariantHash("base.frag", vshadersystem::ShaderStage::eFrag, {});
        auto fragmentShader      = shaderLib.load(fragmentVariantHash, vshadersystem::ShaderStage::eFrag);

        // Create graphics pipeline
        m_GraphicsPipeline = rhi::GraphicsPipeline::Builder {}
                                 .setColorFormats({swapchain.getPixelFormat()})
                                 .setDepthStencil({
                                     .depthTest  = true,
                                     .depthWrite = true,
                                 })
                                 .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
                                 .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
                                 .setRasterizer({.polygonMode = rhi::PolygonMode::eFill})
                                 .setBlending(0, {.enabled = false})
                                 .build(rd);
    }

    virtual void render(RenderContext& ctx) override
    {
        auto& renderWorld = ctx.renderWorld;

        // Normal example CPU-Driven rendering flow would be:
        // for (const auto& inst : renderWorld.instances)
        // {
        //     auto& mesh = renderWorld.gpuScene->resources->meshes[inst.meshIndex];
        //     auto& mat  = renderWorld.gpuScene->resources->materials[mesh.materialOffset];
        // }

        // GPU-Driven rendering flow would consume renderWorld.gpuScene->draws + indirectCommands with minimal CPU
        // overhead. Issue indirect draw call.
        auto ds =
            ctx.cb
                .createDescriptorSetBuilder()
                //   .bind(0, rhi::bindings::StorageBuffer {.buffer = &renderWorld.gpuScene->resources->})
                .bind(1, rhi::bindings::StorageBuffer {.buffer = renderWorld.gpuScene->drawBuffer.get()})
                .bind(
                    2,
                    rhi::bindings::StorageBuffer {.buffer = renderWorld.gpuScene->resources->materialTableBuffer.get()})
                .bind(
                    3,
                    rhi::bindings::StorageBuffer {.buffer = renderWorld.gpuScene->resources->materialParams.gpu.get()})
                .build(m_GraphicsPipeline.getDescriptorSetLayout(0));

        ctx.cb.beginRendering(*ctx.framebufferInfo)
            .bindPipeline(m_GraphicsPipeline)
            .bindDescriptorSet(0, ds)
            .drawIndirect(rhi::DrawIndirectInfo {
                .buffer       = &renderWorld.gpuScene->indirectBuffer.value(),
                .firstCommand = 0,
                .commandCount = static_cast<uint32_t>(renderWorld.gpuScene->indirectCommands.size()),
                .gi =
                    rhi::GeometryInfo {
                        .indexBuffer = &renderWorld.gpuScene->resources->geometry.index32,
                        .numIndices  = renderWorld.gpuScene->resources->geometry.indexCountUsed,
                    },
            })
            .endRendering();
    }

private:
    rhi::GraphicsPipeline m_GraphicsPipeline;
};

class DemoAppHost : public AppHost
{
protected:
    void onConfigure(Engine& engine) override
    {
        engine.ctx().config.title = "Vultra Demo App - Triangle Renderer";

        engine.emplaceSubsystem<WindowSystem>();
        engine.emplaceSubsystem<InputSystem>();

        auto triangleRenderer  = createRef<TriangleRenderer>();
        auto baseColorRenderer = createRef<BaseColorRenderer>();

        auto& camSystem = engine.emplaceSubsystem<CameraSystem>();
        // camSystem.addManualCamera({.rendererKey = triangleRenderer->name().data()});
        camSystem.addManualCamera({.rendererKey = baseColorRenderer->name().data()});

        engine.emplaceSubsystem<WorldSystem>();

        engine.emplaceSubsystem<ShaderSystem>();
        auto& backendSystem = engine.emplaceSubsystem<RenderBackendSystem>();
        auto& renderSystem  = engine.emplaceSubsystem<RenderSystem>();
        renderSystem.registerRenderer(triangleRenderer);
        renderSystem.registerRenderer(baseColorRenderer);

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