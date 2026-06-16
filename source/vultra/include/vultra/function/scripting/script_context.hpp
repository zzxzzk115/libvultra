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
    class IRenderUpscalerService;
    class IFrameDebuggerService;
    class IPhysicsService;
    class IAnimationService;
    class IAudioService;
    class IUiService;
    class IImGuiService;
    class IEditorExtensionService;
    class INavigationService;
    class ISaveService;
    class II18nService;

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
        IRenderUpscalerService* renderUpscalerService {nullptr};
        IFrameDebuggerService* frameDebuggerService {nullptr};
        IPhysicsService* physicsService {nullptr};
        IAnimationService* animationService {nullptr};
        IAudioService* audioService {nullptr};
        IUiService* uiService {nullptr};
        IImGuiService* imguiService {nullptr};
        IEditorExtensionService* editorExtensionService {nullptr};
        INavigationService* navService {nullptr};
        ISaveService* saveService {nullptr};
        II18nService* i18nService {nullptr};

        World* world() const;
        bool   isValid(entt::entity e) const;
    };
} // namespace vultra
