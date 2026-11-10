#pragma once

#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    enum class PassResourceKind
    {
        eTexture,
        eBuffer
    };

    struct PassPort
    {
        std::string             name;
        PassResourceKind        kind   = PassResourceKind::eTexture;
        VriFormat               format = VriFormat_Unknown;
        std::optional<uint32_t> sameExtentAsInput;
    };

    enum class PassControl
    {
        eSlider,
        eCheckbox,
        eChoice,
        eReadOnly
    };

    struct PassChoice
    {
        std::string label;
        double      value;
    };

    // Numeric parameters change command recording, not resource layout or graph topology.
    struct PassParameter
    {
        std::string             name;
        double                  defaultValue;
        double                  minimum;
        double                  maximum;
        std::string             label;
        std::string             description;
        PassControl             control = PassControl::eSlider;
        std::vector<PassChoice> choices;
        bool                    shared = false;
    };

    using PassParameters = std::map<std::string, double>;

    class GraphPass
    {
    public:
        virtual ~GraphPass() = default;
        // Append resources, exports and Expected command passes only. Do not compile or modify existing graph state.
        // Call through PassCatalog for transactional failure recovery.
        // The parameter span stays valid with the owning BuiltPass. Read it when recording commands.
        virtual std::expected<std::vector<RenderGraph::Resource>, std::string>
        addPassesExpected(RenderGraph&                           graph,
                          std::string_view                       name,
                          std::span<const RenderGraph::Resource> inputs,
                          std::span<const double>                parameters) = 0;
    };

    struct PassDefinition
    {
        std::string                                        type;
        std::vector<PassPort>                              inputs;
        std::vector<PassPort>                              outputs;
        std::vector<PassParameter>                         parameters;
        uint64_t                                           requiredFeatures = 0;
        std::string                                        displayName;
        std::string                                        description;
        // Retain this instance and its inputs even if no marked output reaches it.
        bool sideEffect = false;
        std::function<std::expected<std::unique_ptr<GraphPass>, std::string>(Device&)> createExpected;
        // CPU-only schema validation; no device feature query, pipeline creation or resource allocation.
        std::expected<std::vector<double>, std::string>
        parameterValuesExpected(std::string_view instance, const PassParameters& supplied = {}) const;
    };

    // Own this state until the graph's final submission completes and its callbacks are discarded.
    struct BuiltPass
    {
        std::string name;
        std::string type;
        // Never resize after construction: recorded callbacks may retain a span into these values.
        std::vector<double>                parameterValues;
        std::unique_ptr<GraphPass>         instance;
        std::vector<RenderGraph::Resource> outputs;
        RenderGraph* graph = nullptr; // Borrowed; callbacks already require the graph and instance to share a lifetime.
        std::vector<std::string> commandPasses;
    };

    // Explicitly owned by the application/context; no process-global registration.
    class PassCatalog
    {
    public:
        // Starts empty. The caller installs only the pass definitions its experiment uses.
        explicit PassCatalog(Device& device);
        void                             add(PassDefinition definition); // Legacy boundary.
        std::expected<void, std::string> addExpected(PassDefinition definition);
        // Validate the complete group before publication; rejection retains existing definition addresses.
        // Successful registration may invalidate references returned by definitions()/definitionExpected().
        std::expected<void, std::string> addDefinitionsExpected(std::vector<PassDefinition> definitions);
        // Bind a context-owned scene factory without changing the port/parameter contract.
        std::expected<void, std::string>
                                                          bindFactoryExpected(std::string_view                                                               type,
                                                                              std::function<std::expected<std::unique_ptr<GraphPass>, std::string>(Device&)> create);
        const PassDefinition& definition(std::string_view type) const;
        std::expected<const PassDefinition*, std::string> definitionExpected(std::string_view type) const;

        std::span<const PassDefinition> definitions() const
        {
            return m_Definitions;
        }

        BuiltPass build(RenderGraph&                           graph,
                        std::string_view                       type,
                        std::string_view                       name,
                        std::span<const RenderGraph::Resource> inputs,
                        const PassParameters&                  parameters = {}) const;

        // Failed construction removes appended callbacks/resources/exports before destroying their owners.
        // Existing compiled non-aliased graphs remain usable after rejection. Call between completed GPU uses.
        std::expected<BuiltPass, std::string> buildExpected(RenderGraph&                           graph,
                                                            std::string_view                       type,
                                                            std::string_view                       name,
                                                            std::span<const RenderGraph::Resource> inputs,
                                                            const PassParameters& parameters = {}) const;

        // After compilation and completed GPU uses. Changed values invalidate dependent histories before commit;
        // unchanged values are a no-op. Invalid values or invalidation failure leave parameters/history unchanged.
        std::expected<void, std::string> setParametersExpected(BuiltPass& pass, const PassParameters& parameters) const;

    private:
        Device&                     m_Device;
        std::vector<PassDefinition> m_Definitions;
    };
} // namespace vultra
