#pragma once

#include <cstdint>
#include <string>

namespace vultra
{
    // Render-layer indices. An entity is on exactly one render layer (MetaComponent::layer); the
    // default layer is 0 and the built-in UI layer is 5.
    constexpr uint32_t kDefaultRenderLayer = 0u;
    constexpr uint32_t kUiRenderLayer      = 5u;

    // Filtering bitmasks built from layer indices. These are what *other* components (camera
    // cullingMask, cooked render-item layerMask) AND against to decide visibility -- they are not
    // stored on the entity itself.
    constexpr uint32_t kRenderLayerDefaultMask = 1u << kDefaultRenderLayer;
    constexpr uint32_t kRenderLayerUiMask      = 1u << kUiRenderLayer;
    constexpr uint32_t kRenderLayerAllMask     = 0xFFFFFFFFu;

    // The single-bit filtering mask for a given layer index.
    [[nodiscard]] inline uint32_t renderLayerMask(uint32_t layerIndex) noexcept
    {
        return layerIndex < 32u ? (1u << layerIndex) : 0u;
    }

    // Base metadata every entity carries: display name, render layer,
    // status flags, and the don't-destroy-on-load marker. These were previously four
    // separate components (Name/Layer/EntityStatus/Persistent); they are merged here because they
    // are universal per-entity metadata, not behaviour. The editor draws this as one header block
    // rather than as ordinary components. Add new universal metadata fields here (the struct is
    // intentionally the single extension point) -- keep stable identity in IDComponent, not here.
    struct MetaComponent
    {
        std::string name;

        // Editor classification tag. Just a name; the set of valid tags is a
        // project-level list edited in Project Settings. "Untagged" is the default.
        std::string tag {"Untagged"};

        // Render layer the entity belongs to: a single index (0-31), default 0. An entity has
        // exactly one layer; the index<->name table is project config. Filtering against camera
        // culling masks goes through renderLayerMask(layer). (Serialized as "layer".)
        uint32_t layer {kDefaultRenderLayer};

        bool active {true};     // logically enabled (and its subtree)
        bool visible {true};    // rendered
        bool locked {false};    // editor: not selectable/editable in the viewport
        bool selectable {true}; // editor: can be picked
        bool isStatic {false};  // marked static (non-moving) for baking/optimization (serialized as "static")

        // DontDestroyOnLoad marker: when true the entity (and its subtree) survives a scene
        // replacement (Scene.load / Scene.instantiate(clearWorld=true)). Set via
        // Scene.dontDestroyOnLoad(entity).
        bool keepOnLoad {false};
    };
} // namespace vultra
