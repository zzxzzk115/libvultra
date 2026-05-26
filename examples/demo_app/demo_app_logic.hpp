#pragma once

#include <vultra/core/base/common_context.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>

namespace vultra::examples
{
    inline void setupDemoScene(Engine& engine)
    {
        auto& sceneService = engine.ctx().services.require<ISceneService>();
        auto& worldService = engine.ctx().services.require<IWorldService>();

        auto& world = worldService.world();
        auto root = sceneService.instantiateScene(world, "res://scenes/test.vmanifest");
        (void)root;

        VULTRA_CLIENT_INFO("Loaded world from scene: \"res://scenes/test.vmanifest\"");
    }
} // namespace vultra::examples
