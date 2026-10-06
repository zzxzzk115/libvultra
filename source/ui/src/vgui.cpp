#include "vgui_style.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/image/image.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/ui/vgui.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vultra
{
    namespace
    {
        // xmake embeds the built-in skin so VGui works without an asset sidecar.
        constexpr unsigned char kSkinPng[] = {
#include "vgui_skin.png.h"
        };

        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        class RmlFiles final : public Rml::FileInterface
        {
        public:
            explicit RmlFiles(const AssetSource* source) :
                m_Source(source)
            {
            }

            std::vector<std::byte> read(const Rml::String& path) const
            {
                return readSourceFile(std::filesystem::path(std::u8string(path.begin(), path.end())), {}, m_Source);
            }

            void reportFailure(const std::string& message)
            {
                if (m_Error.empty())
                {
                    m_Error = message;
                }
                Logger::app().error("Read VGUI resource: {}", message);
            }

            void check()
            {
                if (!m_Error.empty())
                {
                    throw std::runtime_error("Read VGUI resource: " + std::exchange(m_Error, {}));
                }
            }

            Rml::FileHandle Open(const Rml::String& path) override
            {
                try
                {
                    auto file   = std::make_unique<File>();
                    file->bytes = read(path);
                    return reinterpret_cast<Rml::FileHandle>(file.release());
                }
                catch (const std::exception& error)
                {
                    reportFailure(error.what());
                    return 0;
                }
            }

            void Close(Rml::FileHandle handle) override
            {
                delete reinterpret_cast<File*>(handle);
            }

            size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override
            {
                auto&      file  = *reinterpret_cast<File*>(handle);
                const auto count = std::min(size, file.bytes.size() - file.position);
                if (count)
                {
                    std::memcpy(buffer, file.bytes.data() + file.position, count);
                    file.position += count;
                }
                return count;
            }

            bool Seek(Rml::FileHandle handle, long offset, int origin) override
            {
                auto&  file = *reinterpret_cast<File*>(handle);
                size_t base = 0;
                switch (origin)
                {
                    case SEEK_SET:
                        break;
                    case SEEK_CUR:
                        base = file.position;
                        break;
                    case SEEK_END:
                        base = file.bytes.size();
                        break;
                    default:
                        return false;
                }
                if (offset < 0)
                {
                    const auto distance = uint64_t(-(offset + 1)) + 1;
                    if (distance > base)
                    {
                        return false;
                    }
                    file.position = base - size_t(distance);
                }
                else
                {
                    if (uint64_t(offset) > file.bytes.size() - base)
                    {
                        return false;
                    }
                    file.position = base + size_t(offset);
                }
                return true;
            }

            size_t Tell(Rml::FileHandle handle) override
            {
                return reinterpret_cast<File*>(handle)->position;
            }

            size_t Length(Rml::FileHandle handle) override
            {
                return reinterpret_cast<File*>(handle)->bytes.size();
            }

        private:
            struct File
            {
                std::vector<std::byte> bytes;
                size_t                 position = 0;
            };

            const AssetSource* m_Source;
            std::string        m_Error;
        };

        struct Geometry
        {
            std::unique_ptr<Buffer> vertices;
            std::unique_ptr<Buffer> indices;
            uint32_t                indexCount = 0;
        };

        struct UiTexture
        {
            Device&                  device;
            std::unique_ptr<Texture> image;
            VriDescriptorPool*       pool = nullptr;
            VriDescriptorSet*        set  = nullptr;

            ~UiTexture()
            {
                if (pool)
                {
                    device.core.DestroyDescriptorPool(pool);
                }
            }
        };

        struct DrawCommand
        {
            Geometry*       geometry;
            UiTexture*      texture;
            Rml::Vector2f   translation;
            bool            clipped;
            Rml::Rectanglei clip;
        };

        class RmlRenderer final : public Rml::RenderInterface
        {
        public:
            RmlRenderer(Device& device, VriFormat targetFormat, RmlFiles& files) :
                m_Device(device),
                m_Files(files)
            {
                VriDescriptorRangeDesc ranges[2] {{0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment},
                                                  {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment}};
                VriDescriptorSetDesc   set {};
                set.ranges   = ranges;
                set.rangeNum = 2;
                VriPushConstantDesc   push {0, 32, VriShaderStage_Vertex | VriShaderStage_Fragment};
                VriPipelineLayoutDesc layout {};
                layout.descriptorSets   = &set;
                layout.descriptorSetNum = 1;
                layout.pushConstants    = &push;
                layout.pushConstantNum  = 1;
                layout.shaderStages     = VriShaderStage_Vertex | VriShaderStage_Fragment;
                check(m_Device.core.CreatePipelineLayout(m_Device.handle, &layout, &m_Layout), "Create VGUI layout");
                try
                {
                    VriSamplerDesc sampler {};
                    sampler.minFilter    = VriFilter_Linear;
                    sampler.magFilter    = VriFilter_Linear;
                    sampler.addressModeU = VriAddressMode_ClampToEdge;
                    sampler.addressModeV = VriAddressMode_ClampToEdge;
                    sampler.addressModeW = VriAddressMode_ClampToEdge;
                    check(m_Device.core.CreateSampler(m_Device.handle, &sampler, &m_Sampler), "Create VGUI sampler");
                    m_Pipeline = std::make_unique<ShaderPipeline>(
                        m_Device,
                        "builtin/shaders/passes/vgui.slang",
                        std::vector<ShaderEntry> {{"vertexMain", VriShaderStage_Vertex},
                                                  {"fragmentMain", VriShaderStage_Fragment}},
                        [this, targetFormat](std::span<const VriShaderDesc> shaders)
                        {
                            VriVertexStreamDesc    stream {sizeof(Rml::Vertex), 0, VriVertexStepRate_PerVertex};
                            VriVertexAttributeDesc attributes[3] {};
                            attributes[0].format = VriFormat_RG32_SFLOAT;
                            attributes[0].offset = offsetof(Rml::Vertex, position);
                            attributes[1].format = VriFormat_RGBA8_UNORM;
                            attributes[1].offset = offsetof(Rml::Vertex, colour);
                            attributes[2].format = VriFormat_RG32_SFLOAT;
                            attributes[2].offset = offsetof(Rml::Vertex, tex_coord);
                            VriColorAttachmentDesc color {};
                            color.format         = targetFormat;
                            color.colorWriteMask = VriColorWrite_RGBA;
                            color.blend          = {VRI_TRUE,
                                                    VriBlendFactor_One,
                                                    VriBlendFactor_OneMinusSrcAlpha,
                                                    VriBlendOp_Add,
                                                    VriBlendFactor_One,
                                                    VriBlendFactor_OneMinusSrcAlpha,
                                                    VriBlendOp_Add};
                            VriGraphicsPipelineDesc desc {};
                            desc.pipelineCache           = m_Device.pipelineCache;
                            desc.pipelineLayout          = m_Layout;
                            desc.shaders                 = shaders.data();
                            desc.shaderNum               = uint32_t(shaders.size());
                            desc.vertexInput             = {attributes, 3, &stream, 1};
                            desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                            desc.rasterization.cullMode  = VriCullMode_None;
                            desc.rasterization.lineWidth = 1;
                            desc.multisample.sampleNum   = 1;
                            desc.outputMerger.colors     = &color;
                            desc.outputMerger.colorNum   = 1;
                            VriPipeline* result          = nullptr;
                            check(m_Device.core.CreateGraphicsPipeline(m_Device.handle, &desc, &result),
                                  "Create VGUI pipeline");
                            return result;
                        },
                        "builtin/shaders",
                        std::vector<std::filesystem::path> {"builtin/shaders"});
                    const std::array<Rml::byte, 4> white {255, 255, 255, 255};
                    m_White = reinterpret_cast<UiTexture*>(GenerateTexture({white.data(), white.size()}, {1, 1}));
                }
                catch (...)
                {
                    if (m_Sampler)
                    {
                        m_Device.core.DestroyDescriptor(m_Sampler);
                    }
                    m_Device.core.DestroyPipelineLayout(m_Layout);
                    throw;
                }
            }

            ~RmlRenderer() override
            {
                m_Pipeline.reset();
                m_Textures.clear();
                m_RetiredTextures.clear();
                m_Device.core.DestroyDescriptor(m_Sampler);
                m_Device.core.DestroyPipelineLayout(m_Layout);
            }

            Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                        Rml::Span<const int>         indices) override
            {
                if (vertices.empty() || indices.empty())
                {
                    return 0;
                }
                auto geometry = std::make_unique<Geometry>();
                geometry->vertices =
                    makeBuffer(vertices.data(), vertices.size() * sizeof(Rml::Vertex), VriBufferUsage_VertexBuffer);
                std::vector<uint32_t> unsignedIndices(indices.begin(), indices.end());
                geometry->indices    = makeBuffer(unsignedIndices.data(),
                                               unsignedIndices.size() * sizeof(uint32_t),
                                               VriBufferUsage_IndexBuffer);
                geometry->indexCount = uint32_t(indices.size());
                auto* pointer        = geometry.get();
                m_Geometries.emplace(pointer, std::move(geometry));
                return reinterpret_cast<Rml::CompiledGeometryHandle>(pointer);
            }

            void RenderGeometry(Rml::CompiledGeometryHandle handle,
                                Rml::Vector2f               translation,
                                Rml::TextureHandle          texture) override
            {
                auto* geometry = reinterpret_cast<Geometry*>(handle);
                auto* image    = texture ? reinterpret_cast<UiTexture*>(texture) : m_White;
                m_Draws.push_back({geometry, image, translation, m_Clipped, m_Clip});
            }

            void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
            {
                auto* pointer = reinterpret_cast<Geometry*>(handle);
                if (auto node = m_Geometries.extract(pointer))
                {
                    m_RetiredGeometry.push_back(std::move(node.mapped()));
                }
            }

            Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override
            {
                try
                {
                    const auto image = source == "vgui://skin" ? loadPng(std::as_bytes(std::span(kSkinPng))) :
                                                                 loadPng(m_Files.read(source));
                    dimensions       = {int(image.size.width), int(image.size.height)};
                    std::vector<Rml::byte> pixels(image.rgba.size());
                    for (size_t i = 0; i < pixels.size(); i += 4)
                    {
                        const auto alpha = std::clamp(image.rgba[i + 3], 0.0f, 1.0f);
                        for (size_t channel = 0; channel < 3; ++channel)
                        {
                            pixels[i + channel] =
                                Rml::byte(std::clamp(image.rgba[i + channel], 0.0f, 1.0f) * alpha * 255.0f + 0.5f);
                        }
                        pixels[i + 3] = Rml::byte(alpha * 255.0f + 0.5f);
                    }
                    return GenerateTexture(pixels, dimensions);
                }
                catch (const std::exception& error)
                {
                    m_Files.reportFailure("Texture '" + source + "': " + error.what());
                    return 0;
                }
            }

            Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override
            {
                if (dimensions.x <= 0 || dimensions.y <= 0 || source.size() != size_t(dimensions.x) * dimensions.y * 4)
                {
                    return 0;
                }
                auto texture   = std::make_unique<UiTexture>(m_Device);
                auto desc      = colorTexture({uint32_t(dimensions.x), uint32_t(dimensions.y)}, VriFormat_RGBA8_UNORM);
                desc.usage     = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst;
                texture->image = std::make_unique<Texture>(m_Device, desc);
                const uint32_t rowBytes = uint32_t(dimensions.x) * 4;
                const uint32_t pitch    = (rowBytes + 255) & ~255u;
                Buffer         staging(
                    m_Device,
                    {uint64_t(pitch) * dimensions.y, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
                auto* mapped =
                    static_cast<std::byte*>(m_Device.core.MapBuffer(staging.handle, 0, uint64_t(pitch) * dimensions.y));
                if (!mapped)
                {
                    throw std::runtime_error("Map VGUI texture upload");
                }
                for (int y = 0; y < dimensions.y; ++y)
                {
                    std::memcpy(mapped + size_t(y) * pitch, source.data() + size_t(y) * rowBytes, rowBytes);
                }
                m_Device.core.UnmapBuffer(staging.handle);
                Frame frame(m_Device);
                auto* cmd = frame.begin();
                texture->image->transition(
                    cmd,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                VriBufferTextureCopyDesc copy {};
                copy.bufferRowLength   = pitch / 4;
                copy.bufferImageHeight = uint32_t(dimensions.y);
                copy.texture =
                    {0, 0, 1, VriImageAspect_Color, 0, 0, 0, uint32_t(dimensions.x), uint32_t(dimensions.y), 1};
                m_Device.core.CmdUploadBufferToTexture(cmd, texture->image->handle, staging.handle, &copy);
                texture->image->transition(
                    cmd,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
                frame.submitAndWait();
                VriDescriptorPoolDesc pool {};
                pool.descriptorSetMaxNum = 1;
                pool.textureMaxNum       = 1;
                pool.samplerMaxNum       = 1;
                check(m_Device.core.CreateDescriptorPool(m_Device.handle, &pool, &texture->pool),
                      "Create VGUI texture pool");
                check(m_Device.core.AllocateDescriptorSets(texture->pool, m_Layout, 0, &texture->set, 1),
                      "Allocate VGUI texture set");
                const VriDescriptor*         descriptors[2] {texture->image->view(), m_Sampler};
                VriDescriptorRangeUpdateDesc updates[2] {};
                for (uint32_t index = 0; index < 2; ++index)
                {
                    updates[index].descriptors   = &descriptors[index];
                    updates[index].descriptorNum = 1;
                }
                m_Device.core.UpdateDescriptorRanges(texture->set, 0, 2, updates);
                auto* pointer = texture.get();
                m_Textures.emplace(pointer, std::move(texture));
                return reinterpret_cast<Rml::TextureHandle>(pointer);
            }

            void ReleaseTexture(Rml::TextureHandle handle) override
            {
                auto* pointer = reinterpret_cast<UiTexture*>(handle);
                if (auto node = m_Textures.extract(pointer))
                {
                    m_RetiredTextures.push_back(std::move(node.mapped()));
                }
            }

            void EnableScissorRegion(bool enable) override
            {
                m_Clipped = enable;
            }

            void SetScissorRegion(Rml::Rectanglei region) override
            {
                m_Clip = region;
            }

            void beginFrame(Rml::Vector2i logical, Extent framebuffer)
            {
                // The caller completes the previous submission before this frame's update.
                m_RetiredGeometry.clear();
                m_RetiredTextures.clear();
                m_Draws.clear();
                m_Logical     = logical;
                m_Framebuffer = framebuffer;
            }

            void draw(VriCommandBuffer* cmd, Texture& target)
            {
                if (m_Draws.empty())
                {
                    return;
                }
                target.transition(cmd,
                                  {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                                   VriLayout_ColorAttachment,
                                   VriPipelineStage_ColorAttachmentOutput});
                for (const auto& draw : m_Draws)
                {
                    if (draw.geometry->vertices->state.access != VriAccess_VertexBufferRead)
                    {
                        draw.geometry->vertices->transition(cmd,
                                                            {VriAccess_VertexBufferRead, VriPipelineStage_VertexInput});
                    }
                    if (draw.geometry->indices->state.access != VriAccess_IndexBufferRead)
                    {
                        draw.geometry->indices->transition(cmd,
                                                           {VriAccess_IndexBufferRead, VriPipelineStage_VertexInput});
                    }
                }
                beginColorPass(m_Device, cmd, target.view(), {target.desc.width, target.desc.height});
                VriViewport viewport {0, 0, float(target.desc.width), float(target.desc.height), 0, 1};
                m_Device.core.CmdSetViewports(cmd, &viewport, 1);
                m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                m_Device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
                const float scaleX = float(m_Framebuffer.width) / float(m_Logical.x);
                const float scaleY = float(m_Framebuffer.height) / float(m_Logical.y);
                for (const auto& draw : m_Draws)
                {
                    int left   = 0;
                    int top    = 0;
                    int right  = int(m_Framebuffer.width);
                    int bottom = int(m_Framebuffer.height);
                    if (draw.clipped)
                    {
                        left = std::clamp(int(std::floor(draw.clip.Left() * scaleX)), 0, right);
                        top  = std::clamp(int(std::floor(draw.clip.Top() * scaleY)), 0, bottom);
                        right =
                            std::clamp(int(std::ceil((draw.clip.Left() + draw.clip.Width()) * scaleX)), left, right);
                        bottom =
                            std::clamp(int(std::ceil((draw.clip.Top() + draw.clip.Height()) * scaleY)), top, bottom);
                    }
                    if (right == left || bottom == top)
                    {
                        continue;
                    }
                    VriRect scissor {left, top, uint32_t(right - left), uint32_t(bottom - top)};
                    m_Device.core.CmdSetScissors(cmd, &scissor, 1);

                    struct Parameters
                    {
                        float    extent[2];
                        float    translation[2];
                        uint32_t textured;
                        uint32_t padding[3];
                    } parameters {{float(m_Logical.x), float(m_Logical.y)},
                                  {draw.translation.x, draw.translation.y},
                                  uint32_t(draw.texture != m_White),
                                  {0, 0, 0}};

                    m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
                    m_Device.core.CmdSetDescriptorSet(cmd, 0, draw.texture->set);
                    VriVertexBufferBinding binding {draw.geometry->vertices->handle, 0};
                    m_Device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
                    m_Device.core.CmdSetIndexBuffer(cmd, draw.geometry->indices->handle, 0, VriIndexType_UInt32);
                    VriDrawIndexedDesc indexed {draw.geometry->indexCount, 1, 0, 0, 0};
                    m_Device.core.CmdDrawIndexed(cmd, &indexed);
                }
                m_Device.core.CmdEndRendering(cmd);
            }

        private:
            std::unique_ptr<Buffer> makeBuffer(const void* data, size_t size, VriBufferUsageFlags usage)
            {
                auto buffer =
                    std::make_unique<Buffer>(m_Device, VriBufferDesc {size, 0, usage, VriMemoryLocation_HostUpload});
                void* mapped = m_Device.core.MapBuffer(buffer->handle, 0, size);
                if (!mapped)
                {
                    throw std::runtime_error("Map VGUI geometry");
                }
                std::memcpy(mapped, data, size);
                m_Device.core.UnmapBuffer(buffer->handle);
                return buffer;
            }

            Device&                                                    m_Device;
            RmlFiles&                                                  m_Files;
            VriPipelineLayout*                                         m_Layout  = nullptr;
            VriDescriptor*                                             m_Sampler = nullptr;
            std::unique_ptr<ShaderPipeline>                            m_Pipeline;
            UiTexture*                                                 m_White = nullptr;
            std::unordered_map<Geometry*, std::unique_ptr<Geometry>>   m_Geometries;
            std::unordered_map<UiTexture*, std::unique_ptr<UiTexture>> m_Textures;
            std::vector<std::unique_ptr<Geometry>>                     m_RetiredGeometry;
            std::vector<std::unique_ptr<UiTexture>>                    m_RetiredTextures;
            std::vector<DrawCommand>                                   m_Draws;
            Rml::Rectanglei                                            m_Clip {};
            Rml::Vector2i                                              m_Logical {1, 1};
            Extent                                                     m_Framebuffer {1, 1};
            bool                                                       m_Clipped = false;
        };

        class EventListener final : public Rml::EventListener
        {
        public:
            EventListener(Rml::EventId event, std::function<void()> callback) :
                m_Event(event),
                m_Callback(std::move(callback))
            {
            }

            ~EventListener() override
            {
                // Close() defers document destruction; detach before releasing the callback.
                if (auto* element = m_Element.get())
                {
                    element->RemoveEventListener(m_Event, this);
                }
            }

            void OnAttach(Rml::Element* element) override
            {
                m_Element = element->GetObserverPtr();
            }

            void OnDetach(Rml::Element*) override
            {
                m_Element = nullptr;
            }

            void ProcessEvent(Rml::Event&) override
            {
                m_Callback();
            }

        private:
            Rml::ObserverPtr<Rml::Element> m_Element;
            Rml::EventId                   m_Event;
            std::function<void()>          m_Callback;
        };

        Rml::Input::KeyIdentifier toRmlKey(KeyCode key)
        {
            using namespace Rml::Input;
            if (key >= KeyCode::eA && key <= KeyCode::eZ)
            {
                return KeyIdentifier(KI_A + int(key) - int(KeyCode::eA));
            }
            if (key >= KeyCode::eNum0 && key <= KeyCode::eNum9)
            {
                return KeyIdentifier(KI_0 + int(key) - int(KeyCode::eNum0));
            }
            switch (key)
            {
                case KeyCode::eReturn:
                    return KI_RETURN;
                case KeyCode::eEscape:
                    return KI_ESCAPE;
                case KeyCode::eBackspace:
                    return KI_BACK;
                case KeyCode::eTab:
                    return KI_TAB;
                case KeyCode::eSpace:
                    return KI_SPACE;
                case KeyCode::eLeft:
                    return KI_LEFT;
                case KeyCode::eRight:
                    return KI_RIGHT;
                case KeyCode::eUp:
                    return KI_UP;
                case KeyCode::eDown:
                    return KI_DOWN;
                case KeyCode::eDelete:
                    return KI_DELETE;
                case KeyCode::eHome:
                    return KI_HOME;
                case KeyCode::eEnd:
                    return KI_END;
                default:
                    return KI_UNKNOWN;
            }
        }

        int modifiers(const Input& input)
        {
            using namespace Rml::Input;
            int result = 0;
            if (input.isKeyHeld(KeyCode::eLCtrl) || input.isKeyHeld(KeyCode::eRCtrl))
            {
                result |= KM_CTRL;
            }
            if (input.isKeyHeld(KeyCode::eLShift) || input.isKeyHeld(KeyCode::eRShift))
            {
                result |= KM_SHIFT;
            }
            if (input.isKeyHeld(KeyCode::eLAlt) || input.isKeyHeld(KeyCode::eRAlt))
            {
                result |= KM_ALT;
            }
            if (input.isKeyHeld(KeyCode::eLGUI) || input.isKeyHeld(KeyCode::eRGUI))
            {
                result |= KM_META;
            }
            return result;
        }
    } // namespace

    struct VGui::Impl
    {
        Impl(Device& device, Window* window, Extent size, VriFormat format, const AssetSource* source) :
            window(window),
            source(source),
            files(source),
            renderer(device, format, files)
        {
            if (size.empty())
            {
                throw std::invalid_argument("VGUI requires a nonempty extent");
            }
            // RmlUi's file/font interfaces are process-wide and live until Shutdown().
            if (Rml::GetFileInterface())
            {
                throw std::logic_error("VGUI requires exclusive ownership of RmlUi");
            }
            name = "vultra-vgui-" + std::to_string(reinterpret_cast<uintptr_t>(this));
            Rml::SetFileInterface(&files);
            bool initialized = false;
            try
            {
                initialized = Rml::Initialise();
                if (!initialized)
                {
                    throw std::runtime_error("Initialize RmlUi");
                }
                context = Rml::CreateContext(name, {int(size.width), int(size.height)}, &renderer);
                if (!context)
                {
                    throw std::runtime_error("Create RmlUi context");
                }
            }
            catch (...)
            {
                if (initialized)
                {
                    Rml::Shutdown();
                }
                else
                {
                    Rml::SetFileInterface(nullptr);
                }
                throw;
            }
        }

        ~Impl()
        {
            Rml::RemoveContext(name);
            listeners.clear();
            if (textInputActive && window)
            {
                try
                {
                    window->setTextInputEnabled(false);
                }
                catch (const std::exception& error)
                {
                    Logger::app().error("Stop VGUI text input: {}", error.what());
                }
            }
            Rml::Shutdown();
        }

        Window*                                     window;
        const AssetSource*                          source;
        RmlFiles                                    files;
        RmlRenderer                                 renderer;
        std::string                                 name;
        Rml::Context*                               context  = nullptr;
        Rml::ElementDocument*                       document = nullptr;
        std::vector<std::unique_ptr<EventListener>> listeners;
        InputCapture                                capture;
        bool                                        textInputActive = false;
    };

    VGui::VGui(Device& device, Window& window, VriFormat targetFormat, const AssetSource* source) :
        m_Impl(std::make_unique<Impl>(device, &window, window.size(), targetFormat, source))
    {
    }

    VGui::VGui(Device& device, Extent size, VriFormat targetFormat, const AssetSource* source) :
        m_Impl(std::make_unique<Impl>(device, nullptr, size, targetFormat, source))
    {
    }

    VGui::~VGui() = default;

    void VGui::loadFont(const std::filesystem::path& path)
    {
        const auto file   = m_Impl->source ? m_Impl->source->resolve(path) : path;
        const bool loaded = Rml::LoadFontFace(pathText(file));
        m_Impl->files.check();
        if (!loaded)
        {
            throw std::runtime_error("Load VGUI font: " + path.string());
        }
    }

    void VGui::loadDocument(const std::filesystem::path& path)
    {
        const auto file      = m_Impl->source ? m_Impl->source->resolve(path) : path;
        auto*      candidate = m_Impl->context->LoadDocument(pathText(file));
        try
        {
            m_Impl->files.check();
            if (!candidate)
            {
                throw std::runtime_error("Load VGUI document: " + path.string());
            }
            auto defaults = Rml::Factory::InstanceStyleSheetString(detail::kVGuiStyle);
            if (!defaults)
            {
                throw std::runtime_error("Load built-in VGUI style");
            }
            if (const auto* authored = candidate->GetStyleSheetContainer())
            {
                candidate->SetStyleSheetContainer(defaults->CombineStyleSheetContainer(*authored));
            }
            else
            {
                candidate->SetStyleSheetContainer(std::move(defaults));
            }
        }
        catch (...)
        {
            if (candidate)
            {
                candidate->Close();
            }
            throw;
        }
        if (m_Impl->document)
        {
            m_Impl->document->Close();
        }
        m_Impl->listeners.clear();
        m_Impl->document = candidate;
        m_Impl->document->Show();
    }

    void VGui::bindClick(std::string_view elementId, std::function<void()> callback)
    {
        auto* element = m_Impl->document ? m_Impl->document->GetElementById(std::string(elementId)) : nullptr;
        if (!element)
        {
            throw std::invalid_argument("VGUI element not found: " + std::string(elementId));
        }
        auto listener = std::make_unique<EventListener>(Rml::EventId::Click, std::move(callback));
        element->AddEventListener("click", listener.get());
        m_Impl->listeners.push_back(std::move(listener));
    }

    void VGui::bindChange(std::string_view elementId, std::function<void()> callback)
    {
        auto* element = m_Impl->document ? m_Impl->document->GetElementById(std::string(elementId)) : nullptr;
        if (!element)
        {
            throw std::invalid_argument("VGUI element not found: " + std::string(elementId));
        }
        auto listener = std::make_unique<EventListener>(Rml::EventId::Change, std::move(callback));
        element->AddEventListener("change", listener.get());
        m_Impl->listeners.push_back(std::move(listener));
    }

    std::string VGui::value(std::string_view elementId) const
    {
        auto* element = m_Impl->document ? m_Impl->document->GetElementById(std::string(elementId)) : nullptr;
        auto* control = dynamic_cast<Rml::ElementFormControl*>(element);
        if (!control)
        {
            throw std::invalid_argument("VGUI form control not found: " + std::string(elementId));
        }
        return control->GetValue();
    }

    void VGui::setValue(std::string_view elementId, std::string_view value)
    {
        auto* element = m_Impl->document ? m_Impl->document->GetElementById(std::string(elementId)) : nullptr;
        auto* control = dynamic_cast<Rml::ElementFormControl*>(element);
        if (!control)
        {
            throw std::invalid_argument("VGUI form control not found: " + std::string(elementId));
        }
        control->SetValue(std::string(value));
    }

    bool VGui::isChecked(std::string_view elementId) const
    {
        auto* element = m_Impl->document ? m_Impl->document->GetElementById(std::string(elementId)) : nullptr;
        if (!element)
        {
            throw std::invalid_argument("VGUI element not found: " + std::string(elementId));
        }
        return element->HasAttribute("checked");
    }

    void VGui::setText(std::string_view elementId, std::string_view text)
    {
        auto* element = m_Impl->document ? m_Impl->document->GetElementById(std::string(elementId)) : nullptr;
        if (!element)
        {
            throw std::invalid_argument("VGUI element not found: " + std::string(elementId));
        }
        if (element->GetNumChildren() == 1)
        {
            if (auto* node = dynamic_cast<Rml::ElementText*>(element->GetChild(0)))
            {
                if (node->GetText() != text)
                {
                    node->SetText(std::string(text));
                }
                return;
            }
        }
        element->SetInnerRML(Rml::StringUtilities::EncodeRml(std::string(text)));
    }

    void VGui::setChecked(std::string_view elementId, bool checked)
    {
        auto* element = m_Impl->document ? m_Impl->document->GetElementById(std::string(elementId)) : nullptr;
        if (!element)
        {
            throw std::invalid_argument("VGUI element not found: " + std::string(elementId));
        }
        if (element->HasAttribute("checked") != checked)
        {
            if (checked)
            {
                element->SetAttribute("checked", "");
            }
            else
            {
                element->RemoveAttribute("checked");
            }
        }
    }

    void VGui::update(const Input& input, Extent framebuffer)
    {
        const auto size = m_Impl->window ? m_Impl->window->size() : framebuffer;
        if (size.empty() || framebuffer.empty())
        {
            return;
        }
        const Rml::Vector2i logical {int(size.width), int(size.height)};
        m_Impl->context->SetDimensions(logical);
        const int mods = modifiers(input);
        for (int index = int(KeyCode::eA); index < int(KeyCode::eCount); ++index)
        {
            const auto key        = KeyCode(index);
            const auto translated = toRmlKey(key);
            if (translated == Rml::Input::KI_UNKNOWN)
            {
                continue;
            }
            if (input.isKeyPressed(key) || input.isKeyRepeated(key))
            {
                m_Impl->context->ProcessKeyDown(translated, mods);
            }
            if (input.isKeyReleased(key))
            {
                m_Impl->context->ProcessKeyUp(translated, mods);
            }
        }
        if (!input.textInput().empty())
        {
            m_Impl->context->ProcessTextInput(std::string(input.textInput()));
        }
        const auto mouse = input.mousePosition();
        m_Impl->context->ProcessMouseMove(int(mouse.x), int(mouse.y), mods);
        for (const auto [button, index] :
             {std::pair {MouseCode::eLeft, 0}, std::pair {MouseCode::eRight, 1}, std::pair {MouseCode::eMiddle, 2}})
        {
            if (input.isMouseButtonPressed(button))
            {
                m_Impl->context->ProcessMouseButtonDown(index, mods);
            }
            if (input.isMouseButtonReleased(button))
            {
                m_Impl->context->ProcessMouseButtonUp(index, mods);
            }
        }
        const auto wheel = input.mouseScrollDelta();
        if (wheel.x != 0 || wheel.y != 0)
        {
            m_Impl->context->ProcessMouseWheel({-wheel.x, -wheel.y}, mods);
        }
        m_Impl->context->Update();
        m_Impl->capture.mouse    = m_Impl->context->IsMouseInteracting();
        const auto* focused      = m_Impl->context->GetFocusElement();
        const bool  textFocused  = focused && (focused->GetTagName() == "input" || focused->GetTagName() == "textarea");
        m_Impl->capture.keyboard = focused && focused != m_Impl->document;
        if (textFocused != m_Impl->textInputActive)
        {
            if (m_Impl->window)
            {
                m_Impl->window->setTextInputEnabled(textFocused);
            }
            m_Impl->textInputActive = textFocused;
        }
        m_Impl->renderer.beginFrame(logical, framebuffer);
        m_Impl->context->Render();
        m_Impl->files.check();
    }

    void VGui::draw(VriCommandBuffer* cmd, Texture& target)
    {
        m_Impl->renderer.draw(cmd, target);
    }

    InputCapture VGui::inputCapture() const
    {
        return m_Impl->capture;
    }
} // namespace vultra
