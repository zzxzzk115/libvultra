#pragma once

#include "../../examples/offline/color_gain.hpp"
#include "builtin_owner_expected_probe.hpp"
#include "catalog_expected_probe.hpp"

#include <limits>

namespace vultra::test
{
    namespace pass_factories_probe
    {
        struct ComputeFault
        {
            builtin_owner_probe::NativeFault native;

            explicit ComputeFault(Device& device) :
                native(device)
            {
                device.core.CreateComputePipeline = create;
            }

            static VriResult VRI_CALL create(VriDevice*                    device,
                                             const VriComputePipelineDesc* desc,
                                             VriPipeline**                 output)
            {
                auto&      self     = *builtin_owner_probe::NativeFault::s_Active;
                const bool rejected = self.reject(builtin_owner_probe::Failure::ePipeline);
                const auto status   = self.original.CreateComputePipeline(device, desc, output);
                if (status == VriResult_Success && *output)
                {
                    self.owners.insert(*output);
                }
                // Return an actual partial owner to exercise the candidate pipeline's failure cleanup.
                return rejected && status == VriResult_Success ? VriResult_OutOfMemory : status;
            }
        };

        inline std::expected<void, std::string> gain(Device& device, const ShaderProgram& program)
        {
            ComputeFault     faults(device);
            auto&            native     = faults.native;
            const auto       definition = research::colorGainDefinition(&program);
            const std::array failures {builtin_owner_probe::Failure::eLayout,
                                       builtin_owner_probe::Failure::ePool,
                                       builtin_owner_probe::Failure::eSets,
                                       builtin_owner_probe::Failure::ePipeline};
            for (const auto failure : failures)
            {
                native.arm(failure);
                auto rejected = definition.createExpected(device);
                if (rejected || !native.injected || !native.owners.empty())
                {
                    return std::unexpected("Color gain factory failure leaked or accepted a partial native owner");
                }
                native.arm(builtin_owner_probe::Failure::eNone);
                {
                    auto recovered = definition.createExpected(device);
                    if (!recovered)
                    {
                        return std::unexpected(recovered.error());
                    }
                }
                if (!native.owners.empty())
                {
                    return std::unexpected("Recovered color gain factory retained native owners after destruction");
                }
            }
            {
                PassCatalog catalog(device);
                if (auto added = catalog.addExpected(definition); !added)
                {
                    return added;
                }
                auto missing           = definition;
                missing.type           = "missing.factory";
                missing.createExpected = {};
                if (catalog.addExpected(std::move(missing)) || catalog.definitions().size() != 1 ||
                    catalog.bindFactoryExpected("missing", definition.createExpected))
                {
                    return std::unexpected("Missing factory/binding target changed the accepted catalog");
                }
                BuiltPass        pass;
                RenderGraph      graph(device);
                constexpr Extent size {17, 13};
                auto source = graph.createTextureExpected("source", colorTexture(size, VriFormat_RGBA16_SFLOAT));
                if (!source)
                {
                    return std::unexpected(source.error());
                }
                const std::array sourceUses {RenderGraph::Use {*source, Usage::eColorWrite}};
                auto             clear = graph.addPassExpected(
                    "source",
                    sourceUses,
                    [&device, source = *source, size](auto* commands, auto& active) -> std::expected<void, std::string>
                    {
                        auto texture = active.getTextureExpected(source);
                        if (!texture)
                        {
                            return std::unexpected(texture.error());
                        }
                        auto view = (*texture)->viewExpected();
                        if (!view)
                        {
                            return std::unexpected(view.error());
                        }
                        const float value[4] {0.25f, 0.5f, 1, 0.75f};
                        beginColorPass(device, commands, *view, size, value);
                        device.core.CmdEndRendering(commands);
                        return {};
                    });
                if (!clear)
                {
                    return clear;
                }
                const std::array inputs {*source};
                auto             built = catalog.buildExpected(graph, definition.type, "gain", inputs, {{"gain", 0.5}});
                if (!built)
                {
                    return std::unexpected(built.error());
                }
                pass              = std::move(*built);
                const auto output = pass.outputs.front();
                if (auto exported = graph.exportResourceExpected(output); !exported)
                {
                    return exported;
                }
                if (auto compiled = graph.compileExpected(); !compiled)
                {
                    return compiled;
                }
                auto frame = Frame::create(device);
                if (!frame)
                {
                    return std::unexpected(frame.error());
                }
                native.arm(builtin_owner_probe::Failure::eTextureView);
                auto commands = (*frame)->beginExpected();
                if (!commands || graph.executeExpected(*commands) || !native.injected)
                {
                    return std::unexpected("Expected clear/view recording failure escaped the graph boundary");
                }
                if (auto discarded = (*frame)->discardExpected(); !discarded)
                {
                    return discarded;
                }
                native.arm(builtin_owner_probe::Failure::eNone);
                const auto* parameters = pass.parameterValues.data();
                for (const double value : {0.5, 1.0, 2.0, 0.0})
                {
                    if (auto edited = catalog.setParametersExpected(pass, {{"gain", value}}); !edited)
                    {
                        return edited;
                    }
                    if (catalog.setParametersExpected(pass, {{"gain", std::numeric_limits<double>::quiet_NaN()}}) ||
                        pass.parameterValues.data() != parameters || pass.parameterValues.front() != value)
                    {
                        return std::unexpected("Invalid parameter edit changed the accepted stable parameter span");
                    }
                    commands = (*frame)->beginExpected();
                    if (!commands)
                    {
                        return std::unexpected(commands.error());
                    }
                    if (auto recorded = graph.executeExpected(*commands); !recorded)
                    {
                        return recorded;
                    }
                    if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
                    {
                        return completed;
                    }
                    auto texture = graph.getTextureExpected(output);
                    if (!texture)
                    {
                        return std::unexpected(texture.error());
                    }
                    auto image = readbackExpected(device, **texture);
                    if (!image)
                    {
                        return std::unexpected(image.error());
                    }
                    const std::array expected {float(value * 0.25), float(value * 0.5), float(value), 0.75f};
                    if (image->size != size || image->rgba.size() != size_t(size.width) * size.height * 4)
                    {
                        return std::unexpected("Expected gain output dimensions changed");
                    }
                    for (size_t index = 0; index < image->rgba.size(); ++index)
                    {
                        if (image->rgba[index] != expected[index % 4])
                        {
                            return std::unexpected(
                                "Expected gain GPU output differs from its exact linear-color oracle");
                        }
                    }
                }
            }
            if (!native.owners.empty())
            {
                return std::unexpected("Expected gain graph teardown retained native resources");
            }
            std::puts("EXPECTED_PASS_FACTORIES native_failures=4 partial_pipeline=released recording_retry=passed "
                      "frames=4 exact_channels=3536 stable_parameters=retained");
            return {};
        }
    } // namespace pass_factories_probe
} // namespace vultra::test
