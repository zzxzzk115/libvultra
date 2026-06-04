#pragma once

#include "vultra/core/base/uuid.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace vultra::animator_graph
{
    // Parameter kinds drive transition conditions.
    enum class ParameterType : uint8_t
    {
        eFloat,
        eBool,
        eTrigger, // one-shot; auto-resets after being consumed by a transition
    };

    struct Parameter
    {
        std::string   name;
        ParameterType type {ParameterType::eFloat};
        float         defaultFloat {0.0f};
        bool          defaultBool {false};
    };

    // A transition condition compares a parameter against a value.
    enum class ConditionType : uint8_t
    {
        eGreater,  // float > threshold
        eLess,     // float < threshold
        eEqual,    // float ~= threshold
        eNotEqual, // float !~= threshold
        eTrue,     // bool == true
        eFalse,    // bool == false
        eTrigger,  // trigger is set
    };

    struct Condition
    {
        std::string   parameter;
        ConditionType type {ConditionType::eGreater};
        float         threshold {0.0f};
    };

    struct Transition
    {
        std::string            to;          // destination state name
        std::vector<Condition> conditions;  // ANDed together
        float                  duration {0.2f};   // cross-fade seconds
        bool                   hasExitTime {false};
        float                  exitTime {1.0f};    // normalized [0,1] of source clip
    };

    struct State
    {
        std::string             name;
        CoreUUID                animation; // clip asset
        float                   speed {1.0f};
        bool                    loop {true};
        std::vector<Transition> transitions;
        nlohmann::json          editor; // node editor layout (pos, etc.)
    };

    struct Graph
    {
        uint32_t                version {1};
        std::string             name;
        std::string             entry; // entry state name (defaults to first state)
        std::vector<Parameter>  parameters;
        std::vector<State>      states;
        std::vector<Transition> anyTransitions; // evaluated from any state ("Any State")
        nlohmann::json          metadata;

        [[nodiscard]] const State* findState(std::string_view name) const;
        [[nodiscard]] int          stateIndex(std::string_view name) const;
        [[nodiscard]] const Parameter* findParameter(std::string_view name) const;
    };

    // JSON (canonical .vanimgraph.json). `diagnostics` collects human-readable errors.
    std::optional<Graph> graphFromJson(const nlohmann::json& root, std::vector<std::string>* diagnostics = nullptr);
    nlohmann::json       graphToJson(const Graph& graph);
    std::optional<Graph> loadGraphFromText(std::string_view text, std::vector<std::string>* diagnostics = nullptr);
    std::string          saveGraphToText(const Graph& graph);

    // Structural validation (unknown transition targets, missing entry, duplicate names, ...).
    std::vector<std::string> validateGraph(const Graph& graph);
} // namespace vultra::animator_graph
