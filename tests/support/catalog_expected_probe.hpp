#pragma once

#include "import_bindings_probe.hpp"

#include <vultra/servers/rendering/graph/graph_definition.hpp>

#include <array>
#include <iostream>
#include <utility>

namespace vultra::test
{
    namespace catalog_probe
    {
        enum class Failure
        {
            eReject,
            eCount,
            eForeign,
            eBuffer,
            eFormat,
            eExtent,
            eSideEffect,
            eLegacyRecording,
            eNone
        };

        struct State
        {
            int  callbacks    = 0;
            int  owners       = 0;
            bool orderValid   = true;
        };

        // Count every std::function-held copy, including temporary callback copies.
        struct Callback
        {
            State* state;

            explicit Callback(State& value) :
                state(&value)
            {
                ++state->callbacks;
            }

            Callback(const Callback& value) :
                state(value.state)
            {
                ++state->callbacks;
            }

            Callback(Callback&& value) noexcept :
                state(std::exchange(value.state, nullptr))
            {
            }

            ~Callback()
            {
                if (state)
                {
                    --state->callbacks;
                }
            }

            std::expected<void, std::string> operator()(VriCommandBuffer*, RenderGraph&) const
            {
                return {};
            }
        };

        class Probe final : public GraphPass
        {
        public:
            Probe(State& state, Failure failure) :
                m_State(state),
                m_Failure(failure)
            {
                ++m_State.owners;
            }

            ~Probe() override
            {
                m_State.orderValid = m_State.orderValid && m_State.callbacks == 0;
                --m_State.owners;
            }

            std::expected<std::vector<RenderGraph::Resource>, std::string>
            addPassesExpected(RenderGraph&                           graph,
                              std::string_view                       name,
                              std::span<const RenderGraph::Resource> inputs,
                              std::span<const double>) override
            {
                auto desc = graph.resourceInfo(inputs.front()).textureDesc;
                if (m_Failure == Failure::eFormat)
                {
                    desc.format = VriFormat_RGBA32_SFLOAT;
                }
                if (m_Failure == Failure::eExtent)
                {
                    ++desc.width;
                }
                auto output = graph.createTextureExpected(std::string(name) + ".candidate", desc);
                if (!output)
                {
                    return std::unexpected(output.error());
                }
                if (auto exported = graph.exportResourceExpected(*output); !exported)
                {
                    return std::unexpected(exported.error());
                }
                const std::array uses {RenderGraph::Use {*output, Usage::eColorWrite}};
                if (m_Failure == Failure::eLegacyRecording)
                {
                    graph.addPass(std::string(name),
                                  uses,
                                  [hold = Callback(m_State)](auto*, auto&)
                                  {
                                      static_cast<void>(hold);
                                  });
                    return std::vector<RenderGraph::Resource> {*output};
                }
                auto             added = graph.addPassExpected(std::string(name),
                                                   uses,
                                                   Callback(m_State),
                                                   m_Failure == Failure::eSideEffect);
                if (!added)
                {
                    return std::unexpected(added.error());
                }
                if (m_Failure == Failure::eReject)
                {
                    return std::unexpected("injected append rejection");
                }
                if (m_Failure == Failure::eCount)
                {
                    return std::vector<RenderGraph::Resource> {};
                }
                if (m_Failure == Failure::eForeign)
                {
                    return std::vector<RenderGraph::Resource> {{nullptr, 0}};
                }
                if (m_Failure == Failure::eBuffer)
                {
                    VriBufferDesc buffer {};
                    buffer.size   = 16;
                    buffer.usage  = VriBufferUsage_TransferDst;
                    auto resource = graph.createBufferExpected(std::string(name) + ".buffer", buffer);
                    if (!resource)
                    {
                        return std::unexpected(resource.error());
                    }
                    return std::vector<RenderGraph::Resource> {*resource};
                }
                return std::vector<RenderGraph::Resource> {*output};
            }

        private:
            State&  m_State;
            Failure m_Failure;
        };

        inline PassDefinition definition(std::string type, State& state, Failure failure)
        {
            PassDefinition value;
            value.type           = std::move(type);
            value.inputs         = {{"src", PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT}};
            value.outputs        = {{"dst", PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT, 0}};
            value.createExpected = [&state, failure](Device&) -> std::expected<std::unique_ptr<GraphPass>, std::string>
            {
                return std::make_unique<Probe>(state, failure);
            };
            return value;
        }
    } // namespace catalog_probe

    inline std::expected<void, std::string> catalogExpectedProbe(Device& device)
    {
        using namespace catalog_probe;
        State state;
        State firstState;
        // These states outlive graph and every callback, including a failing regression's cleanup.
        const VriBufferDesc bufferDesc {16,
                                        0,
                                        VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                                        VriMemoryLocation_Device};
        auto                imported = Buffer::create(device, bufferDesc);
        auto                trial    = Buffer::create(device, bufferDesc);
        if (!imported || !trial)
        {
            return std::unexpected(imported ? trial.error() : imported.error());
        }
        GraphBuild  kept;
        RenderGraph graph(device);
        auto                            binding = graph.importResourceExpected("bound", **imported);
        const std::array<std::byte, 16> initial {};
        auto                            history = graph.createHistoryBufferExpected("history", bufferDesc, initial);
        if (!binding || !history)
        {
            return std::unexpected(binding ? history.error() : binding.error());
        }
        if (auto exported = graph.exportResourceExpected(*history); !exported)
        {
            return exported;
        }
        if (auto added = graph.addPassExpected("before prefix",
                                               {},
                                               [](auto*, auto&) -> std::expected<void, std::string>
                                               {
                                                   return {};
                                               });
            !added)
        {
            return added;
        }
        auto        source = graph.createTextureExpected("prefix", colorTexture({5, 3}, VriFormat_RGBA16_SFLOAT));
        if (!source)
        {
            return std::unexpected(source.error());
        }
        int              executions = 0;
        const std::array uses {RenderGraph::Use {*source, Usage::eColorWrite}};
        auto             prefix = graph.addPassExpected(
            "prefix",
            uses,
            [&device, &executions, source = *source](auto* commands, auto& active) -> std::expected<void, std::string>
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
                const float color[4] {.25f, .5f, 2, 1};
                beginColorPass(device, commands, *view, {5, 3}, color);
                device.core.CmdEndRendering(commands);
                ++executions;
                return {};
            });
        if (!prefix)
        {
            return prefix;
        }
        if (auto exported = graph.exportResourceExpected(*source); !exported)
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
        const auto runPrefix = [&]() -> std::expected<void, std::string>
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                auto discarded = (*frame)->discardExpected();
                if (!discarded)
                {
                    return discarded;
                }
                return recorded;
            }
            return (*frame)->submitAndWaitExpected();
        };
        if (auto ran = runPrefix(); !ran)
        {
            return ran;
        }
        auto baseline        = graph.snapshotExpected();
        auto originalTexture = graph.getTextureExpected(*source);
        auto originalHistory = graph.bufferHistoryInfoExpected(*history, {});
        if (!baseline || !originalTexture || !originalHistory || originalHistory->resetPending)
        {
            return std::unexpected("Catalog prefix did not compile and initialize its history");
        }
        const auto epoch     = graph.historyEpoch();
        const auto unchanged = [&]()
        {
            const auto snapshot       = graph.snapshotExpected();
            const auto currentTexture = graph.getTextureExpected(*source);
            const auto currentBinding = graph.getBufferExpected(*binding);
            const auto currentHistory = graph.bufferHistoryInfoExpected(*history, {});
            return snapshot && snapshot->resources.size() == baseline->resources.size() &&
                   snapshot->passes.size() == baseline->passes.size() &&
                   snapshot->passes.back().dependencies == baseline->passes.back().dependencies &&
                   snapshot->exports.size() == baseline->exports.size() &&
                   snapshot->resources[source->index].name == "prefix" &&
                   graph.activePasses() == std::vector<std::string> {"prefix"} &&
                   !graph.findResourceExpected("candidate.candidate") && snapshot->resources[source->index].exported &&
                   currentTexture && *currentTexture == *originalTexture && currentBinding &&
                   *currentBinding == imported->get() && currentHistory &&
                   currentHistory->epoch == originalHistory->epoch && !currentHistory->resetPending &&
                   graph.historyEpoch() == epoch && state.callbacks == 0 && state.owners == 0 && state.orderValid;
        };
        PassCatalog catalog(device);
        auto        desc = definition("probe", state, Failure::eNone);
        if (auto added = catalog.addExpected(desc); !added)
        {
            return added;
        }
        if (catalog.addExpected(desc) || catalog.definitionExpected("missing") ||
            catalog.bindFactoryExpected("probe", {}))
        {
            return std::unexpected("Expected catalog schema/binding rejection failed");
        }
        const std::array inputs {*source};
        const std::array failures {Failure::eReject,
                                   Failure::eCount,
                                   Failure::eForeign,
                                   Failure::eBuffer,
                                   Failure::eFormat,
                                   Failure::eExtent,
                                   Failure::eSideEffect,
                                   Failure::eLegacyRecording};
        for (const auto failure : failures)
        {
            auto replacement = definition("unused", state, failure);
            if (auto bound = catalog.bindFactoryExpected("probe", std::move(replacement.createExpected)); !bound)
            {
                return bound;
            }
            auto rejected = catalog.buildExpected(graph, "probe", "candidate", inputs);
            if (rejected || !unchanged())
            {
                return std::unexpected(
                    "Rejected catalog append retained callbacks/resources or destroyed its owner first");
            }
            if (failure == Failure::eLegacyRecording &&
                rejected.error().find("Expected command callbacks") == std::string::npos)
            {
                return std::unexpected("Legacy recording callback was not rejected at the construction boundary");
            }
            if (auto ran = runPrefix(); !ran)
            {
                return ran;
            }
        }
        for (const bool nullInstance : {false, true})
        {
            auto bound = catalog.bindFactoryExpected(
                "probe",
                [nullInstance](Device&) -> std::expected<std::unique_ptr<GraphPass>, std::string>
                {
                    if (nullInstance)
                    {
                        return std::unique_ptr<GraphPass> {};
                    }
                    return std::unexpected("injected factory rejection");
                });
            if (!bound)
            {
                return bound;
            }
            if (catalog.buildExpected(graph, "probe", "candidate", inputs) || !unchanged())
            {
                return std::unexpected("Rejected/null Expected factory changed the compiled graph");
            }
        }
        auto checkpoint = graph.checkpointExpected();
        if (!checkpoint)
        {
            return std::unexpected(checkpoint.error());
        }
        const auto rejectedWith = [](const auto& result, std::string_view text)
        {
            return !result && result.error().find(text) != std::string::npos;
        };
        import_bindings_probe::NativeCalls native(device);
        auto trialFactory = [&](Device&) -> std::expected<std::unique_ptr<GraphPass>, std::string>
        {
            const std::string_view construction = "during graph construction";
            const std::string_view prefix       = "predates this graph construction";
            const std::array       changed {std::string_view("prefix")};
            if (!rejectedWith(graph.compileExpected(), construction) ||
                !rejectedWith(graph.executeExpected(nullptr), construction) ||
                !rejectedWith(graph.checkpointExpected(), construction) ||
                !rejectedWith(graph.restoreUnsubmittedExpected(std::move(*checkpoint)), construction) ||
                !rejectedWith(graph.bindExpected(*binding, **trial), construction) ||
                !rejectedWith(graph.resetHistoryExpected(), construction) ||
                !rejectedWith(graph.resetHistoryExpected(std::span(&*history, 1)), construction) ||
                !rejectedWith(graph.resetDependentHistoryExpected(changed), construction) ||
                !rejectedWith(graph.nameResourceExpected(*source, "wrong"), prefix) ||
                !rejectedWith(graph.addOrderingExpected("before prefix", "prefix"), prefix) ||
                !rejectedWith(graph.captureAfterPassExpected("prefix", *source, "wrong"), prefix))
            {
                return std::unexpected("Catalog construction allowed a prefix or state mutation");
            }

            auto appended = graph.importResourceExpected("trial", **trial);
            if (!appended)
            {
                return std::unexpected(appended.error());
            }
            const std::array writes {RenderGraph::Use {*appended, Usage::eCopyDestination, true}};
            if (!graph.nameResourceExpected(*appended, "renamed trial") ||
                !graph.addPassExpected("trial writer", writes, Callback(state)) ||
                !graph.addOrderingExpected("prefix", "trial writer") ||
                !graph.captureAfterPassExpected("trial writer", *appended, "trial capture") ||
                !rejectedWith(graph.compileHeapsExpected(), construction) || native.names != 0)
            {
                return std::unexpected("Catalog construction did not isolate its append suffix");
            }
            return std::unexpected("injected factory append rejection");
        };
        if (auto bound = catalog.bindFactoryExpected("probe", trialFactory); !bound)
        {
            return bound;
        }
        if (auto result = catalog.buildExpected(graph, "probe", "candidate", inputs);
            !rejectedWith(result, "injected factory append rejection") || !unchanged() || native.names != 0)
        {
            return std::unexpected(result ? "Catalog accepted a rejected factory append" : result.error());
        }
        auto first = definition("first", firstState, Failure::eNone);
        auto createFirst     = std::move(first.createExpected);
        first.createExpected = [&](Device& gpu) -> std::expected<std::unique_ptr<GraphPass>, std::string>
        {
            auto imported = graph.importResourceExpected("committed import", **trial);
            if (!imported)
            {
                return std::unexpected(imported.error());
            }
            if (auto added = graph.addPassExpected("factory command", {}, Callback(firstState)); !added)
            {
                return std::unexpected(added.error());
            }
            return createFirst(gpu);
        };
        if (auto added = catalog.addExpected(std::move(first)); !added)
        {
            return added;
        }
        auto rejectNested = [&](Device&) -> std::expected<std::unique_ptr<GraphPass>, std::string>
        {
            auto prior = graph.findResourceExpected("first.candidate");
            if (!prior || native.names != 0 ||
                !rejectedWith(graph.nameResourceExpected(*prior, "wrong nested"), "predates this graph construction") ||
                !rejectedWith(graph.addOrderingExpected("prefix", "first"), "predates this graph construction"))
            {
                return std::unexpected("Inner construction changed the outer append suffix");
            }
            return std::unexpected("injected nested rejection");
        };
        if (auto bound = catalog.bindFactoryExpected("probe", rejectNested); !bound)
        {
            return bound;
        }
        GraphDefinition  chain {{{"first", "first", {}}, {"candidate", "probe", {}}},
                                {{"scene.src", "first.src"}, {"first.dst", "candidate.src"}},
                                {"candidate.dst"}};
        const std::array imports {GraphBinding {"scene.src", *source}};
        if (auto result = chain.buildExpected(graph, catalog, imports);
            !rejectedWith(result, "injected nested rejection") || !unchanged() || native.names != 0 ||
            firstState.callbacks || firstState.owners || !firstState.orderValid)
        {
            return std::unexpected(result ? "Definition accepted a rejected nested append" : result.error());
        }
        if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpoint)); !restored)
        {
            return restored;
        }
        if (auto ran = runPrefix(); !ran)
        {
            return ran;
        }
        auto pixels = readbackExpected(device, **originalTexture);
        if (!pixels)
        {
            return std::unexpected(pixels.error());
        }
        const std::array<float, 4> color {.25f, .5f, 2, 1};
        if (pixels->rgba.size() != 5 * 3 * color.size())
        {
            return std::unexpected("Recovered catalog prefix returned the wrong image size");
        }
        for (size_t channel = 0; channel < pixels->rgba.size(); ++channel)
        {
            if (pixels->rgba[channel] != color[channel % color.size()])
            {
                return std::unexpected("Recovered catalog prefix returned different GPU pixels");
            }
        }
        const auto labelsBeforeCommit = native.names;
        auto recovered = definition("unused", state, Failure::eNone);
        if (auto bound = catalog.bindFactoryExpected("probe", std::move(recovered.createExpected)); !bound)
        {
            return bound;
        }
        auto built = chain.buildExpected(graph, catalog, imports);
        if (!built || built->passes.size() != 2 || built->outputs.size() != 1 ||
            built->passes.front().commandPasses != std::vector<std::string> {"factory command", "first"} ||
            native.names != labelsBeforeCommit + 1)
        {
            return std::unexpected("Expected definition did not recover after rejection");
        }
        kept = std::move(*built);
        // Successful callbacks remain graph-owned until graph destruction, before kept's instance owners.
        if (!state.callbacks || !firstState.callbacks || executions != 10)
        {
            return std::unexpected("Expected graph recovery lost callback ownership or compiled prefix execution");
        }
        std::cout << "Expected catalog: append boundary, nested rollback, checkpoint and native label publication, "
                     "owner order, compiled-prefix GPU reuse and definition recovery passed\n";
        return {};
    }
} // namespace vultra::test
