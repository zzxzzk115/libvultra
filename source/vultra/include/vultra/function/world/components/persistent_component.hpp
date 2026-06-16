#pragma once

namespace vultra
{
    // DontDestroyOnLoad marker: an entity with this component (and its subtree) survives a
    // scene replacement (Scene.load / Scene.instantiate(clearWorld=true)). Used for managers,
    // the player, persistent audio, etc. Mark via Scene.dontDestroyOnLoad(entity).
    struct PersistentComponent
    {
        bool keepOnLoad {true};
    };
} // namespace vultra
