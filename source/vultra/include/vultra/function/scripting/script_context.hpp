#pragma once

#include <entt/entity/fwd.hpp>

namespace vultra
{
    class World;
    class IWorldService;
    class IInputService;
    class IAssetService;

    struct ScriptContext
    {
        IWorldService* worldService {nullptr};
        IInputService* inputService {nullptr};
        IAssetService* assetService {nullptr};

        World* world() const;
        bool   isValid(entt::entity e) const;
    };
} // namespace vultra
