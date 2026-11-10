#pragma once

#include <vultra/servers/rendering/builtin/raster_passes.hpp>
#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>

#include <cstdio>
#include <limits>

namespace vultra::test
{
    namespace pass_registration_probe
    {
        struct State
        {
            size_t owners = 0;
            size_t calls  = 0;
        };

        struct Factory
        {
            explicit Factory(State& state) :
                state(&state)
            {
                ++state.owners;
            }

            Factory(const Factory& other) :
                Factory(*other.state)
            {
            }

            ~Factory()
            {
                --state->owners;
            }

            std::expected<std::unique_ptr<GraphPass>, std::string> operator()(Device&) const
            {
                ++state->calls;
                return std::unexpected("Retained metadata factory");
            }

            State* state;
        };

        inline PassDefinition definition(std::string type, State& state)
        {
            PassDefinition result;
            result.type           = std::move(type);
            result.description    = "Retained metadata";
            result.createExpected = Factory(state);
            return result;
        }
    } // namespace pass_registration_probe

    inline std::expected<void, std::string> passRegistrationProbe(Device& device)
    {
        using namespace pass_registration_probe;
        State       state;
        PassCatalog catalog(device);
        if (auto added = catalog.addExpected(definition("seed", state)); !added)
        {
            return added;
        }
        const auto* retained = catalog.definitions().data();
        size_t      rejected = 0;
        for (unsigned fault = 0; fault < 12; ++fault)
        {
            auto registration = [&]()
            {
                std::vector<PassDefinition> definitions;
                definitions.push_back(definition("first", state));
                auto last = definition("last", state);
                switch (fault)
                {
                    case 0:
                        last.type = "first";
                        break;
                    case 1:
                        last.type = "seed";
                        break;
                    case 2:
                        last.createExpected = {};
                        break;
                    case 3:
                        last.outputs = {{"bad", static_cast<PassResourceKind>(99)}};
                        break;
                    case 4:
                        last.outputs = {{"bad", PassResourceKind::eBuffer, VriFormat_RGBA8_UNORM}};
                        break;
                    case 5:
                        last.outputs = {{"bad", PassResourceKind::eTexture, VriFormat_Unknown, 0}};
                        break;
                    case 6:
                        last.outputs = {{"same"}, {"same"}};
                        break;
                    case 7:
                        last.parameters = {{"bad", std::numeric_limits<double>::quiet_NaN(), 0, 1}};
                        break;
                    case 8:
                        last.parameters = {{"bad", 2, 0, 1}};
                        break;
                    case 9:
                        last.parameters                 = {{"bad", 0, 0, 1}};
                        last.parameters.front().control = PassControl::eChoice;
                        break;
                    case 10:
                        last.parameters = {{"same", 0, 0, 1}, {"same", 0, 0, 1}};
                        break;
                    case 11:
                        last.parameters                 = {{"bad", 0, 0, 1}};
                        last.parameters.front().control = static_cast<PassControl>(99);
                        break;
                }
                definitions.push_back(std::move(last));
                return catalog.addDefinitionsExpected(std::move(definitions));
            }();
            if (registration || catalog.definitions().size() != 1 || catalog.definitions().data() != retained ||
                catalog.definitions().front().description != "Retained metadata" || state.owners != 1 ||
                state.calls != 0)
            {
                return std::unexpected("Rejected definition group published a prefix or retained candidate factories");
            }
            ++rejected;
        }
        auto invalid = catalog.addExpected(definition("", state));
        if (invalid || catalog.definitions().data() != retained || state.owners != 1)
        {
            return std::unexpected("Single registration bypassed checked group validation");
        }
        if (!catalog.addDefinitionsExpected({}) || catalog.definitions().data() != retained)
        {
            return std::unexpected("Empty definition group invalidated the existing catalog");
        }
        RenderGraph graph(device);
        auto        called = catalog.buildExpected(graph, "seed", "retained", {});
        if (called || called.error().find("Retained metadata factory") == std::string::npos || state.calls != 1)
        {
            return std::unexpected("Rejected registration changed the retained factory");
        }
        if (auto added = catalog.addDefinitionsExpected({definition("first", state), definition("last", state)});
            !added)
        {
            return added;
        }
        if (catalog.definitions().size() != 3 || catalog.definitions()[1].type != "first" ||
            catalog.definitions()[2].type != "last" || state.owners != 3 || state.calls != 1)
        {
            return std::unexpected("Valid group registration changed order, factory ownership or invoked a factory");
        }
        auto builtins = builtinRasterPassDefinitions();
        builtins.insert(builtins.begin(), toneMappingDefinition());
        for (const auto& conflict : builtins)
        {
            PassCatalog candidate(device);
            auto        seed = definition(conflict.type, state);
            if (auto added = candidate.addExpected(std::move(seed)); !added)
            {
                return added;
            }
            const auto* before = candidate.definitions().data();
            if (installBuiltinRenderPassesExpected(candidate) || candidate.definitions().size() != 1 ||
                candidate.definitions().data() != before ||
                candidate.definitions().front().description != "Retained metadata")
            {
                return std::unexpected("Built-in installer published definitions before a later conflict");
            }
            ++rejected;
        }
        PassCatalog installed(device);
        if (auto added = installBuiltinRenderPassesExpected(installed); !added)
        {
            return added;
        }
        if (installed.definitions().size() != 5 || rejected != 17 || state.owners != 3 || state.calls != 1)
        {
            return std::unexpected("Built-in installation recovery changed definitions or retained failed owners");
        }
        std::puts("EXPECTED_PASS_REGISTRATION rejected_groups=17 single_rejection=passed builtin_conflicts=5 "
                  "validation_factory_calls=0 retained_factory=callable owner_cleanup=passed recovery=passed");
        return {};
    }
} // namespace vultra::test
