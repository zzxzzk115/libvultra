#pragma once

#include <vbase/service/service_registry.hpp>

#include <string_view>

namespace vultra
{
    class World;

    // Scene service (asset-facing).
    // - Owns/loads/saves the scene asset (.vscn)
    // - Can instantiate an asset into a World.
    // - MUST NOT own the World.
    class ISceneService
    {
    public:
        SERVICE_REGISTER(ISceneService)

        virtual ~ISceneService() = default;

        virtual bool hasSceneLoaded() const = 0;

        // Load or replace the currently loaded scene asset.
        virtual bool loadSceneSync(std::string_view uri) = 0;

        // Save the currently loaded scene asset.
        virtual bool saveSceneSync(std::string_view uri) const = 0;

        // Instantiate current scene into a world.
        // If clearWorld is true, the target world will be cleared first.
        virtual bool instantiateToWorld(World& world, bool clearWorld) const = 0;
    };
} // namespace vultra
