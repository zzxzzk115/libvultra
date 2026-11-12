#include "../common/sample.hpp"

#include <vultra/assets/scene_data.hpp>
#include <vultra/core/math/ray.hpp>
#include <vultra/gpu/buffer_upload.hpp>
#include <vultra/shader/shader_pipeline.hpp>
#include <vultra/xr/openxr.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

// Stereo rendering and controller input through the xr module and VRI, without a window or a render path.
// The floor, a car, both hands and a pointer beam per controller are drawn once per eye.
namespace
{
    using vultra::Error;

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
    };

    struct Geometry
    {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
    };

    struct Part
    {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    struct Model
    {
        Part      part;
        glm::vec3 color {1};
    };

    struct Draw
    {
        Part      part;
        glm::mat4 model {1};
        glm::vec3 color {1};
        bool      grid = false;
    };

    // Keep in sync with Parameters in scene.slang.
    struct Parameters
    {
        glm::mat4 transform;
        glm::vec4 light;
        glm::vec4 color;
        glm::vec4 options;
    };

    Part appendQuad(Geometry& geometry, glm::vec3 center, glm::vec3 halfU, glm::vec3 halfV)
    {
        const auto      first  = uint32_t(geometry.vertices.size());
        const auto      start  = uint32_t(geometry.indices.size());
        const glm::vec3 normal = glm::normalize(glm::cross(halfU, halfV));
        for (const auto corner :
             {center - halfU - halfV, center + halfU - halfV, center + halfU + halfV, center - halfU + halfV})
        {
            geometry.vertices.push_back({corner, normal});
        }
        for (const uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
        {
            geometry.indices.push_back(first + index);
        }
        return {start, 6};
    }

    // A cube with unit edges centred on the origin.
    Part appendBox(Geometry& geometry)
    {
        const auto start = uint32_t(geometry.indices.size());
        for (int axis = 0; axis < 3; ++axis)
        {
            glm::vec3 normal {0};
            glm::vec3 u {0};
            glm::vec3 v {0};
            normal[axis]      = 0.5f;
            u[(axis + 1) % 3] = 0.5f;
            v[(axis + 2) % 3] = 0.5f;
            appendQuad(geometry, normal, u, v);
            appendQuad(geometry, -normal, v, u);
        }
        return {start, 36};
    }

    std::expected<Model, Error> appendObj(Geometry& geometry, const std::filesystem::path& path)
    {
        const auto scene = vultra::loadObj(path);
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        if (scene->primitives.empty())
        {
            return vultra::makeError("{} has no geometry", path.string());
        }
        const auto first = uint32_t(geometry.vertices.size());
        const auto start = uint32_t(geometry.indices.size());
        for (const auto& vertex : scene->vertices)
        {
            geometry.vertices.push_back({vertex.position, vertex.normal});
        }
        for (const auto index : scene->indices)
        {
            geometry.indices.push_back(first + index);
        }
        const auto& material = scene->materials.at(scene->primitives.front().material);
        return Model {{start, uint32_t(scene->indices.size())}, glm::vec3(material.baseColor)};
    }

    // One pipeline and one vertex/index buffer pair for every model. The pipeline builder keeps this object's
    // address, so it lives behind a unique_ptr.
    class Renderer
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<Renderer>, Error>
        create(vultra::Device& device, VriFormat format, const Geometry& geometry)
        {
            auto vertices = vultra::uploadBuffer(device,
                                                 std::as_bytes(std::span(geometry.vertices)),
                                                 VriBufferUsage_VertexBuffer,
                                                 {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
            if (!vertices)
            {
                return std::unexpected(vertices.error());
            }
            auto indices = vultra::uploadBuffer(device,
                                                std::as_bytes(std::span(geometry.indices)),
                                                VriBufferUsage_IndexBuffer,
                                                {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
            if (!indices)
            {
                return std::unexpected(indices.error());
            }
            VriPushConstantDesc   push {0, sizeof(Parameters), VriShaderStage_Vertex | VriShaderStage_Fragment};
            VriPipelineLayoutDesc layoutDesc {};
            layoutDesc.pushConstants   = &push;
            layoutDesc.pushConstantNum = 1;
            layoutDesc.shaderStages    = push.shaderStages;
            VriPipelineLayout* layout  = nullptr;
            if (auto created = vultra::check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layout),
                                             "Create XR scene layout");
                !created)
            {
                return std::unexpected(created.error());
            }
            // From here the renderer owns the layout and releases it if the pipeline fails.
            std::unique_ptr<Renderer> renderer(
                new Renderer(device, std::move(*vertices), std::move(*indices), layout, format));
            const auto build =
                [self = renderer.get()](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
            {
                VriVertexStreamDesc    stream {sizeof(Vertex), 0, VriVertexStepRate_PerVertex};
                VriVertexAttributeDesc attributes[2] {};
                attributes[0].format = VriFormat_RGB32_SFLOAT;
                attributes[0].offset = offsetof(Vertex, position);
                attributes[1].format = VriFormat_RGB32_SFLOAT;
                attributes[1].offset = offsetof(Vertex, normal);
                VriColorAttachmentDesc color {};
                color.format         = self->m_Format;
                color.colorWriteMask = VriColorWrite_RGBA;

                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache          = self->m_Device.pipelineCache;
                desc.pipelineLayout         = self->m_Layout;
                desc.shaders                = shaders.data();
                desc.shaderNum              = uint32_t(shaders.size());
                desc.vertexInput            = {attributes, 2, &stream, 1};
                desc.inputAssembly.topology = VriPrimitiveTopology_TriangleList;
                // The right hand is the mirrored left hand, which reverses its winding.
                desc.rasterization.cullMode          = VriCullMode_None;
                desc.rasterization.lineWidth         = 1;
                desc.multisample.sampleNum           = 1;
                desc.depthStencil.depthTest          = VRI_TRUE;
                desc.depthStencil.depthWrite         = VRI_TRUE;
                desc.depthStencil.depthCompareOp     = VriCompareOp_LessOrEqual;
                desc.outputMerger.colors             = &color;
                desc.outputMerger.colorNum           = 1;
                desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                VriPipeline* result                  = nullptr;
                if (auto created =
                        vultra::check(self->m_Device.core.CreateGraphicsPipeline(self->m_Device.handle, &desc, &result),
                                      "Create XR scene pipeline");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                return result;
            };
            auto pipeline = vultra::ShaderPipeline::create(
                device,
                "examples/xr/scene.slang",
                {{"vertexMain", VriShaderStage_Vertex}, {"fragmentMain", VriShaderStage_Fragment}},
                build);
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            renderer->m_Pipeline = std::move(*pipeline);
            return renderer;
        }

        ~Renderer()
        {
            m_Pipeline.reset();
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }

        Renderer(const Renderer&)            = delete;
        Renderer& operator=(const Renderer&) = delete;

        [[nodiscard]] std::expected<void, Error>
        draw(VriCommandBuffer* cmd, const vultra::XREye& eye, uint32_t index, std::span<const Draw> draws)
        {
            const auto viewProjection = eye.viewProjection();
            if (!viewProjection)
            {
                return std::unexpected(viewProjection.error());
            }
            auto&                target = *eye.color;
            const vultra::Extent size {target.desc.width, target.desc.height};
            auto&                depth = m_Depth.at(index);
            if (!depth)
            {
                auto created =
                    vultra::Texture::create(m_Device, vultra::depthTexture(size), nullptr, VriImageAspect_Depth);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                depth = std::move(*created);
            }
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            depth->transition(cmd,
                              {VriAccess_DepthStencilAttachmentWrite,
                               VriLayout_DepthStencilAttachment,
                               VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
            const auto colorView = target.view();
            if (!colorView)
            {
                return std::unexpected(colorView.error());
            }
            const auto depthView = depth->view();
            if (!depthView)
            {
                return std::unexpected(depthView.error());
            }
            const glm::vec3   sky = encode({0.02f, 0.03f, 0.05f});
            VriAttachmentDesc colorAttachment {};
            colorAttachment.view    = *colorView;
            colorAttachment.loadOp  = VriAttachmentLoadOp_Clear;
            colorAttachment.storeOp = VriAttachmentStoreOp_Store;
            std::copy_n(&sky.x, 3, colorAttachment.clearValue.color.f32);
            colorAttachment.clearValue.color.f32[3] = 1;
            VriAttachmentDesc depthAttachment {};
            depthAttachment.view                          = *depthView;
            depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
            depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
            depthAttachment.clearValue.depthStencil.depth = 1;
            VriAttachmentsDesc attachments {};
            attachments.colors     = &colorAttachment;
            attachments.colorNum   = 1;
            attachments.depth      = &depthAttachment;
            attachments.renderArea = {0, 0, size.width, size.height};
            attachments.layerNum   = 1;
            m_Device.core.CmdBeginRendering(cmd, &attachments);
            const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
            const VriRect     scissor {0, 0, size.width, size.height};
            m_Device.core.CmdSetViewports(cmd, &viewport, 1);
            m_Device.core.CmdSetScissors(cmd, &scissor, 1);
            m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
            m_Device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
            const VriVertexBufferBinding binding {m_Vertices.handle, 0};
            m_Device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
            m_Device.core.CmdSetIndexBuffer(cmd, m_Indices.handle, 0, VriIndexType_UInt32);
            const glm::vec3 toLight = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
            for (const auto& item : draws)
            {
                // The light direction goes to model space instead of the normals going to world space.
                const Parameters parameters {
                    *viewProjection * item.model,
                    glm::vec4(glm::normalize(glm::transpose(glm::mat3(item.model)) * toLight), 0),
                    glm::vec4(item.color, 1),
                    glm::vec4(item.grid ? 1.0f : 0.0f, linearTarget() ? 1.0f : 0.0f, 0, 0)};
                m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
                const VriDrawIndexedDesc indexed {item.part.indexCount, 1, item.part.firstIndex, 0, 0};
                m_Device.core.CmdDrawIndexed(cmd, &indexed);
            }
            m_Device.core.CmdEndRendering(cmd);
            return {};
        }

        void pollShader()
        {
            m_Pipeline->poll();
        }

    private:
        Renderer(vultra::Device&    device,
                 vultra::Buffer     vertices,
                 vultra::Buffer     indices,
                 VriPipelineLayout* layout,
                 VriFormat          format) :
            m_Device(device),
            m_Vertices(std::move(vertices)),
            m_Indices(std::move(indices)),
            m_Layout(layout),
            m_Format(format)
        {
        }

        // A UNORM eye image stores what the shader writes, so the shader encodes; an sRGB image encodes itself.
        bool linearTarget() const
        {
            return m_Format == VriFormat_RGBA8_UNORM || m_Format == VriFormat_BGRA8_UNORM;
        }

        glm::vec3 encode(glm::vec3 color) const
        {
            return linearTarget() ? glm::pow(color, glm::vec3(1 / 2.2f)) : color;
        }

        vultra::Device&                               m_Device;
        vultra::Buffer                                m_Vertices;
        vultra::Buffer                                m_Indices;
        VriPipelineLayout*                            m_Layout = nullptr;
        VriFormat                                     m_Format;
        std::unique_ptr<vultra::ShaderPipeline>       m_Pipeline;
        std::array<std::optional<vultra::Texture>, 2> m_Depth;
    };

    std::expected<void, Error> run(const sample::Options& options)
    {
        using namespace vultra;
        auto system = OpenXRSystem::create("Vultra XR example");
        if (!system)
        {
            return std::unexpected(system.error());
        }
        auto device = Device::create(true, (*system)->creationHooks());
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto session = OpenXRSession::create(**system, **device);
        if (!session)
        {
            return std::unexpected(session.error());
        }
        XrInstanceProperties runtime {XR_TYPE_INSTANCE_PROPERTIES};
        xrGetInstanceProperties((*system)->instance(), &runtime);
        Logger::app().info("OpenXR runtime: {}; eye format {}", runtime.runtimeName, int((*session)->format()));

        Geometry   geometry;
        const Part floor = appendQuad(geometry, {0, 0, 0}, {0, 0, 20}, {20, 0, 0});
        const Part box   = appendBox(geometry);
        const auto hand  = appendObj(geometry, "resources/models/OpenXRVulkanExample/Hand.obj");
        if (!hand)
        {
            return std::unexpected(hand.error());
        }
        const auto car = appendObj(geometry, "resources/models/OpenXRVulkanExample/Car.obj");
        if (!car)
        {
            return std::unexpected(car.error());
        }
        auto renderer = Renderer::create(**device, (*session)->format(), geometry);
        if (!renderer)
        {
            return std::unexpected(renderer.error());
        }
        auto frame = Frame::create(**device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }

        // The tracking space has its origin at the head's starting position, so the floor sits below it.
        constexpr float kFloorHeight = -1.5f;
        const glm::mat4 world        = glm::translate(glm::mat4(1), {0, kFloorHeight, 0});
        const glm::mat4 carPose = glm::rotate(glm::translate(world, {-1.5f, 0, -5.0f}), glm::radians(60.0f), {0, 1, 0});
        constexpr std::array<const char*, 2> kHandNames {"Left", "Right"};

        uint64_t                rendered = 0;
        std::array<uint64_t, 2> tracked {};
        std::array<bool, 2>     pressed {};
        std::vector<Draw>       draws;
        while (!(*session)->exiting() && (!options.frames || rendered < options.frames))
        {
            (*renderer)->pollShader();
            const auto xr = (*session)->beginFrame();
            if (!xr)
            {
                return std::unexpected(xr.error());
            }
            if (!xr->begun)
            {
                // The session is not running yet, or no longer.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            draws.clear();
            draws.push_back({floor, world, {0.12f, 0.14f, 0.16f}, true});
            draws.push_back({car->part, carPose, car->color});
            for (size_t index = 0; index < 2; ++index)
            {
                const auto& controller = xr->hands[index];
                if (!controller.active)
                {
                    continue;
                }
                ++tracked[index];
                const bool select = controller.select > 0.5f;
                if (select != pressed[index])
                {
                    Logger::app().info("{} hand select {}", kHandNames[index], select ? "pressed" : "released");
                    pressed[index] = select;
                }
                // The hand model is a left hand authored in the aim pose.
                const glm::mat4 mirror = glm::scale(glm::mat4(1), {index == 0 ? 1.0f : -1.0f, 1, 1});
                draws.push_back({hand->part, controller.aim * mirror, hand->color});
                draws.push_back({box,
                                 glm::scale(controller.grip, glm::vec3(0.03f * (1 + controller.squeeze))),
                                 {0.9f, 0.2f, 0.2f}});

                const Ray       pointer = controller.pointer();
                const auto      hit     = intersectPlane(pointer, {0, kFloorHeight, 0}, {0, 1, 0});
                const float     length  = hit ? std::min(*hit, 10.0f) : 10.0f;
                const glm::vec3 color   = select ? glm::vec3(1.0f, 0.5f, 0.1f) : glm::vec3(0.8f, 0.9f, 1.0f);
                draws.push_back(
                    {box,
                     glm::scale(glm::translate(controller.aim, {0, 0, -length / 2}), {0.004f, 0.004f, length}),
                     color});
                if (hit && *hit <= 10.0f)
                {
                    draws.push_back(
                        {box, glm::scale(glm::translate(glm::mat4(1), pointer.at(*hit)), {0.1f, 0.01f, 0.1f}), color});
                }
            }

            if (xr->shouldRender)
            {
                const auto cmd = (*frame)->begin();
                if (!cmd)
                {
                    return std::unexpected(cmd.error());
                }
                for (uint32_t eye = 0; eye < 2; ++eye)
                {
                    if (auto drawn = (*renderer)->draw(*cmd, xr->eyes[eye], eye, draws); !drawn)
                    {
                        return drawn;
                    }
                }
                // --capture saves the left eye of the last frame, or of the first one without --frames.
                std::optional<ImageReadback> capture;
                if (!options.capture.empty() && rendered + 1 == std::max<uint64_t>(options.frames, 1))
                {
                    auto readback = ImageReadback::create(**device, *xr->eyes[0].color);
                    if (!readback)
                    {
                        return std::unexpected(readback.error());
                    }
                    capture.emplace(std::move(*readback));
                    if (auto recorded = capture->record(*cmd, *xr->eyes[0].color); !recorded)
                    {
                        return recorded;
                    }
                }
                (*session)->prepareSubmit(*cmd);
                if (auto submitted = (*frame)->submitAndWait(); !submitted)
                {
                    return submitted;
                }
                if (capture)
                {
                    const auto image = capture->consume();
                    if (!image)
                    {
                        return std::unexpected(image.error());
                    }
                    if (auto saved = savePng(*image, options.capture); !saved)
                    {
                        return saved;
                    }
                }
                ++rendered;
            }
            if (auto ended = (*session)->endFrame(); !ended)
            {
                return ended;
            }
        }
        Logger::app().info("XR: {} rendered frames; controllers tracked in {} (left) and {} (right) frames",
                           rendered,
                           tracked[0],
                           tracked[1]);
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return sample::fail(options.error());
    }
    if (!*options)
    {
        return 0;
    }
    if (auto result = run(**options); !result)
    {
        return sample::fail(result.error());
    }
    return 0;
}
