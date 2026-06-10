#pragma once

#include <entt/entity/fwd.hpp>

namespace vultra
{
    class World;
    class IWorldService;
    class IInputService;
    class ITimingService;
    class IAssetService;
    class ISceneService;
    class IScriptService;
    class ICameraService;
    class IRenderService;
    class IRenderBackendService;
    class IFrameDebuggerService;
    class IPhysicsService;
    class IAnimationService;
    class IAudioService;
    class IUiService;

    struct ScriptContext
    {
        IWorldService* worldService {nullptr};
        IInputService* inputService {nullptr};
        ITimingService* timingService {nullptr};
        IAssetService* assetService {nullptr};
        ISceneService* sceneService {nullptr};
        IScriptService* scriptService {nullptr};
        ICameraService* cameraService {nullptr};
        IRenderService* renderService {nullptr};
        IRenderBackendService* renderBackendService {nullptr};
        IFrameDebuggerService* frameDebuggerService {nullptr};
        IPhysicsService* physicsService {nullptr};
        IAnimationService* animationService {nullptr};
        IAudioService* audioService {nullptr};
        IUiService* uiService {nullptr};

        World* world() const;
        bool   isValid(entt::entity e) const;
    };
} // namespace vultra
