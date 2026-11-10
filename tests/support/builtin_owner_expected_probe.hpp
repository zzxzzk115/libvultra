#pragma once

#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>

namespace vultra::test
{
    namespace builtin_owner_probe
    {
        enum class Failure
        {
            eNone,
            eBuffer,
            eTexture,
            eBufferView,
            eTextureView,
            eSampler,
            eLayout,
            ePool,
            eSets,
            ePipeline
        };

        struct NativeFault
        {
            Device&                    device;
            VriCoreInterface           original;
            Failure                    failure          = Failure::eNone;
            uint32_t                   ordinal          = 1;
            uint32_t                   calls            = 0;
            uint32_t                   creates          = 0;
            bool                       injected         = false;
            bool                       denyLinearFilter = false;
            std::set<const void*>      owners;
            inline static NativeFault* s_Active = nullptr;

            explicit NativeFault(Device& value) :
                device(value),
                original(value.core)
            {
                s_Active                           = this;
                device.core.GetFormatSupport       = formatSupport;
                device.core.CreateBuffer           = createBuffer;
                device.core.CreateTexture          = createTexture;
                device.core.CreateBufferView       = createBufferView;
                device.core.CreateTextureView      = createTextureView;
                device.core.CreateSampler          = createSampler;
                device.core.CreatePipelineLayout   = createLayout;
                device.core.CreateDescriptorPool   = createPool;
                device.core.AllocateDescriptorSets = allocateSets;
                device.core.CreateGraphicsPipeline = createPipeline;
                device.core.DestroyBuffer          = destroyBuffer;
                device.core.DestroyTexture         = destroyTexture;
                device.core.DestroyDescriptor      = destroyDescriptor;
                device.core.DestroyPipelineLayout  = destroyLayout;
                device.core.DestroyDescriptorPool  = destroyPool;
                device.core.DestroyPipeline        = destroyPipeline;
            }

            ~NativeFault()
            {
                device.core = original;
                s_Active    = nullptr;
            }

            void arm(Failure value, uint32_t occurrence = 1)
            {
                failure  = value;
                ordinal  = occurrence;
                calls    = 0;
                injected = false;
            }

            bool reject(Failure value)
            {
                ++creates;
                if (failure == value && ++calls == ordinal)
                {
                    injected = true;
                    return true;
                }
                return false;
            }

            static VriFormatSupportFlags VRI_CALL formatSupport(const VriDevice* device, VriFormat format)
            {
                const auto flags = s_Active->original.GetFormatSupport(device, format);
                return s_Active->denyLinearFilter ? flags & ~VriFormatSupport_LinearFilter : flags;
            }

            static VriResult VRI_CALL createBuffer(VriDevice* device, const VriBufferDesc* desc, VriBuffer** output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::eBuffer))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreateBuffer(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static VriResult VRI_CALL createTexture(VriDevice* device, const VriTextureDesc* desc, VriTexture** output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::eTexture))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreateTexture(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static VriResult VRI_CALL createBufferView(VriDevice*               device,
                                                       const VriBufferViewDesc* desc,
                                                       VriDescriptor**          output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::eBufferView))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreateBufferView(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static VriResult VRI_CALL createTextureView(VriDevice*                device,
                                                        const VriTextureViewDesc* desc,
                                                        VriDescriptor**           output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::eTextureView))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreateTextureView(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static VriResult VRI_CALL createSampler(VriDevice*            device,
                                                    const VriSamplerDesc* desc,
                                                    VriDescriptor**       output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::eSampler))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreateSampler(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static VriResult VRI_CALL createLayout(VriDevice*                   device,
                                                   const VriPipelineLayoutDesc* desc,
                                                   VriPipelineLayout**          output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::eLayout))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreatePipelineLayout(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static VriResult VRI_CALL createPool(VriDevice*                   device,
                                                 const VriDescriptorPoolDesc* desc,
                                                 VriDescriptorPool**          output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::ePool))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreateDescriptorPool(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static VriResult VRI_CALL allocateSets(VriDescriptorPool*       pool,
                                                   const VriPipelineLayout* layout,
                                                   uint32_t                 index,
                                                   VriDescriptorSet**       output,
                                                   uint32_t                 count)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::eSets))
                {
                    std::fill_n(output, count, nullptr);
                    return VriResult_OutOfMemory;
                }
                return self.original.AllocateDescriptorSets(pool, layout, index, output, count);
            }

            static VriResult VRI_CALL createPipeline(VriDevice*                     device,
                                                     const VriGraphicsPipelineDesc* desc,
                                                     VriPipeline**                  output)
            {
                auto& self = *s_Active;
                if (self.reject(Failure::ePipeline))
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto result = self.original.CreateGraphicsPipeline(device, desc, output);
                if (result == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                return result;
            }

            static void VRI_CALL destroyBuffer(VriBuffer* value)
            {
                s_Active->owners.erase(value);
                s_Active->original.DestroyBuffer(value);
            }

            static void VRI_CALL destroyTexture(VriTexture* value)
            {
                s_Active->owners.erase(value);
                s_Active->original.DestroyTexture(value);
            }

            static void VRI_CALL destroyDescriptor(VriDescriptor* value)
            {
                s_Active->owners.erase(value);
                s_Active->original.DestroyDescriptor(value);
            }

            static void VRI_CALL destroyLayout(VriPipelineLayout* value)
            {
                s_Active->owners.erase(value);
                s_Active->original.DestroyPipelineLayout(value);
            }

            static void VRI_CALL destroyPool(VriDescriptorPool* value)
            {
                s_Active->owners.erase(value);
                s_Active->original.DestroyDescriptorPool(value);
            }

            static void VRI_CALL destroyPipeline(VriPipeline* value)
            {
                s_Active->owners.erase(value);
                s_Active->original.DestroyPipeline(value);
            }
        };
    } // namespace builtin_owner_probe

    inline std::expected<void, std::string> builtinOwnerExpectedProbe(Device&            device,
                                                                      GpuScene&          scene,
                                                                      Environment&       environment,
                                                                      const AssetSource* shaders = nullptr)
    {
        using namespace builtin_owner_probe;
        // Views belong to the borrowed scene, and are not candidate renderer allocations.
        for (const auto& textures : scene.materialTextures)
        {
            for (auto* texture : textures)
            {
                if (auto view = texture->viewExpected(); !view)
                {
                    return std::unexpected(view.error());
                }
            }
        }
        NativeFault faults(device);
        if (BuiltinRenderer::create(device, scene, environment, VriFormat_R32_SFLOAT, shaders) || faults.creates)
        {
            return std::unexpected("Built-in invalid output format reached native allocation");
        }
        faults.denyLinearFilter = true;
        auto unsupportedFilter  = BuiltinRenderer::create(device, scene, environment, VriFormat_RGBA8_UNORM, shaders);
        faults.denyLinearFilter = false;
        if (unsupportedFilter || faults.creates ||
            unsupportedFilter.error().find("linear filtering") == std::string::npos)
        {
            return std::unexpected(
                "Built-in rendering did not reject unsupported environment filtering before allocation");
        }
        {
            auto live = BuiltinRenderer::create(device, scene, environment, VriFormat_RGBA8_UNORM, shaders);
            if (!live)
            {
                return std::unexpected(live.error());
            }
            const auto       accepted = faults.owners;
            const std::array cases {std::pair {Failure::eBuffer, 1u},
                                    std::pair {Failure::eBuffer, 2u},
                                    std::pair {Failure::eTexture, 1u},
                                    std::pair {Failure::eBufferView, 1u},
                                    std::pair {Failure::eBufferView, 2u},
                                    std::pair {Failure::eSampler, 1u},
                                    std::pair {Failure::eSampler, 2u},
                                    std::pair {Failure::eLayout, 1u},
                                    std::pair {Failure::eLayout, 2u},
                                    std::pair {Failure::ePool, 1u},
                                    std::pair {Failure::ePool, 2u},
                                    std::pair {Failure::eSets, 1u},
                                    std::pair {Failure::eSets, 2u},
                                    std::pair {Failure::eSets, 3u},
                                    std::pair {Failure::ePipeline, 1u},
                                    std::pair {Failure::ePipeline, 18u},
                                    std::pair {Failure::ePipeline, 19u}};
            for (const auto [failure, ordinal] : cases)
            {
                faults.arm(failure, ordinal);
                auto rejected = BuiltinRenderer::create(device, scene, environment, VriFormat_RGBA8_UNORM, shaders);
                if (rejected || !faults.injected || faults.owners != accepted)
                {
                    return std::unexpected("Built-in partial initialization leaked or replaced accepted owners: " +
                                           std::to_string(uint32_t(failure)) + "/" + std::to_string(ordinal));
                }
            }
            faults.arm(Failure::eNone);
            auto recovery = BuiltinRenderer::create(device, scene, environment, VriFormat_RGBA8_UNORM, shaders);
            if (!recovery)
            {
                return std::unexpected(recovery.error());
            }
            recovery->reset();
            if (faults.owners != accepted)
            {
                return std::unexpected("Recovered built-in renderer retained native owners");
            }
        }
        if (!faults.owners.empty())
        {
            return std::unexpected("Built-in renderer destruction leaked native owners");
        }
        std::puts(
            "Expected built-in ownership: 17 native initialization rejections, accepted owners retained and recovery");
        return {};
    }

    inline std::expected<void, std::string> environmentFilterExpectedProbe(Device& device, const ShaderProgram& program)
    {
        using namespace builtin_owner_probe;
        TextureData data;
        data.format = TextureFormat::eRgba32Sfloat;
        const std::array<float, 4> color {0.1f, 0.1f, 0.1f, 1};
        TextureLevel               level;
        level.size = {1, 1};
        level.bytes.resize(sizeof(color));
        std::memcpy(level.bytes.data(), color.data(), sizeof(color));
        data.levels.push_back(std::move(level));
        NativeFault faults(device);
        faults.denyLinearFilter = true;
        faults.arm(Failure::ePipeline);
        auto rejected = Environment::create(device, data, &program);
        if (rejected || faults.injected || !faults.owners.empty() ||
            rejected.error().find("linear filtering") == std::string::npos)
        {
            return std::unexpected(
                "Environment preprocessing did not reject unsupported filtering before pipeline creation");
        }
        faults.denyLinearFilter = false;
        faults.arm(Failure::eNone);
        const bool filterable =
            (device.core.GetFormatSupport(device.handle, VriFormat_RGBA32_SFLOAT) & VriFormatSupport_LinearFilter) != 0;
        if (!filterable)
        {
            auto nativeRejected = Environment::create(device, data, &program);
            if (nativeRejected || !faults.owners.empty() ||
                nativeRejected.error().find("linear filtering") == std::string::npos)
            {
                return std::unexpected("Unsupported native FP32 environment filtering was not rejected");
            }
        }
        std::printf("Expected environment filtering: synthetic rejection and native FP32 filterable=%u\n",
                    unsigned(filterable));
        return {};
    }

    inline std::expected<void, std::string> toneMappingExpectedProbe(Device&            device,
                                                                     const AssetSource* shaders = nullptr)
    {
        using namespace builtin_owner_probe;
        constexpr Extent size {7, 5};
        TextureData      data;
        data.format = TextureFormat::eRgba32Sfloat;
        std::vector<float>             pixels(size_t(size.width) * size.height * 4);
        constexpr std::array<float, 4> color {0.002f, 0.5f, 2.0f, 1.0f};
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            pixels[i] = color[i % 4];
        }
        TextureLevel level;
        level.size = size;
        level.bytes.resize(pixels.size() * sizeof(float));
        std::memcpy(level.bytes.data(), pixels.data(), level.bytes.size());
        data.levels.push_back(std::move(level));
        auto input = uploadTextureAssetExpected(device, data);
        if (!input)
        {
            return std::unexpected(input.error());
        }
        if (auto view = (*input)->viewExpected(); !view)
        {
            return std::unexpected(view.error());
        }
        NativeFault faults(device);
        if (ToneMappingPass::create(device, VriFormat_R32_SFLOAT, shaders) || faults.creates)
        {
            return std::unexpected("Tone mapping invalid format reached native allocation");
        }
        for (const auto failure : {Failure::eLayout, Failure::ePool, Failure::eSets, Failure::ePipeline})
        {
            faults.arm(failure);
            auto rejected = ToneMappingPass::create(device, VriFormat_RGBA8_UNORM, shaders);
            if (rejected || !faults.injected || !faults.owners.empty())
            {
                return std::unexpected("Tone mapping native initialization rejection leaked owners");
            }
        }
        faults.arm(Failure::eNone);
        for (const auto format : {VriFormat_RGBA8_UNORM, VriFormat_BGRA8_UNORM, VriFormat_RGBA16_SFLOAT})
        {
            auto pass = ToneMappingPass::create(device, format, shaders);
            if (!pass)
            {
                return std::unexpected(pass.error());
            }
            std::array<double, 3> parameters {0, 0, 1};
            RenderGraph           graph(device);
            auto                  source = graph.importResourceExpected("input", **input);
            if (!source)
            {
                return std::unexpected(source.error());
            }
            const std::array inputs {*source};
            if (auto exported = graph.exportResourceExpected(*source); !exported)
            {
                return std::unexpected(exported.error());
            }
            if (auto compiled = graph.compileExpected(); !compiled)
            {
                return std::unexpected(compiled.error());
            }
            const auto  count = graph.snapshot().resources.size();
            RenderGraph foreign(device);
            auto        other = foreign.importResourceExpected("foreign", **input);
            if (!other)
            {
                return std::unexpected(other.error());
            }
            const std::array foreignInputs {*other};
            if ((*pass)->addPassesExpected(graph, "foreign", foreignInputs, parameters) ||
                (*pass)->addPassesExpected(graph, "missing", {}, parameters) ||
                graph.snapshot().resources.size() != count)
            {
                return std::unexpected("Tone mapping rejected append changed its graph");
            }
            auto output = (*pass)->addPassesExpected(graph, "tone", inputs, parameters);
            if (!output)
            {
                return std::unexpected(output.error());
            }
            if (auto exported = graph.exportResourceExpected(output->front()); !exported)
            {
                return std::unexpected(exported.error());
            }
            if (auto compiled = graph.compileExpected(); !compiled)
            {
                return std::unexpected(compiled.error());
            }
            auto frame = Frame::create(device);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            auto render = [&]() -> std::expected<Image, std::string>
            {
                auto commands = (*frame)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                auto recorded = graph.executeExpected(*commands);
                if (!recorded)
                {
                    auto discarded = (*frame)->discardExpected();
                    if (!discarded)
                    {
                        return std::unexpected(discarded.error());
                    }
                    return std::unexpected(recorded.error());
                }
                if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
                {
                    return std::unexpected(completed.error());
                }
                auto texture = graph.getTextureExpected(output->front());
                if (!texture)
                {
                    return std::unexpected(texture.error());
                }
                return readback(device, **texture);
            };
            // Inject an output-view failure before this pass records rendering commands.
            faults.arm(Failure::eTextureView);
            auto rejected = render();
            if (rejected || !faults.injected)
            {
                return std::unexpected("Tone mapping view failure was swallowed");
            }
            faults.arm(Failure::eNone);
            Image accepted;
            for (const auto values : {std::array<double, 3> {0, 0, 1}, {1, 0, 2}, {0, 0, 0}, {3, 1, 0}})
            {
                parameters = values;
                auto image = render();
                if (!image)
                {
                    return std::unexpected(image.error());
                }
                for (size_t i = 0; i < image->rgba.size(); ++i)
                {
                    double expected = 1;
                    if (i % 4 != 3)
                    {
                        expected = color[i % 4];
                        if (parameters[1] < 0.5)
                        {
                            expected *= std::exp2(parameters[0]);
                            if (parameters[2] == 0)
                            {
                                expected = std::clamp(expected * (2.51 * expected + 0.03) /
                                                          (expected * (2.43 * expected + 0.59) + 0.14),
                                                      0.0,
                                                      1.0);
                            }
                            else if (parameters[2] == 2)
                            {
                                expected /= 1 + expected;
                            }
                        }
                        if (format != VriFormat_RGBA16_SFLOAT)
                        {
                            expected = expected <= 0.0031308 ? expected * 12.92 :
                                                               1.055 * std::pow(expected, 1.0 / 2.4) - 0.055;
                            expected = std::clamp(expected, 0.0, 1.0);
                        }
                    }
                    const auto tolerance = format == VriFormat_RGBA16_SFLOAT ? 0.002 : 0.005;
                    if (!std::isfinite(image->rgba[i]) || std::abs(image->rgba[i] - expected) > tolerance)
                    {
                        return std::unexpected(
                            "Tone mapping GPU output differs from the independent display/linear reference");
                    }
                }
                accepted = std::move(*image);
            }
            parameters[0] = std::numeric_limits<double>::quiet_NaN();
            if (render())
            {
                return std::unexpected("Tone mapping accepted nonfinite live parameters");
            }
            auto texture = graph.getTextureExpected(output->front());
            if (!texture || readback(device, **texture).rgba != accepted.rgba)
            {
                return std::unexpected("Rejected tone mapping recording changed accepted pixels");
            }
            parameters     = {3, 1, 0};
            auto recovered = render();
            if (!recovered || recovered->rgba != accepted.rgba)
            {
                return std::unexpected("Tone mapping did not recover its accepted output");
            }
        }
        if (!faults.owners.empty())
        {
            return std::unexpected("Tone mapping graph/pass destruction leaked owners");
        }
        std::puts("Expected tone mapping: 3 formats x 4 analytic GPU outputs, view/parameter rejection and recovery");
        return {};
    }
} // namespace vultra::test
