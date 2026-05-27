#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"

namespace vultra
{
    class XRRuntimeSystem final : public EngineSubsystem
    {
    public:
        ENGINE_SUBSYSTEM(XRRuntimeSystem)

        bool onInit() override;
        void onShutdown() override;
        void onUpdate(fsec dt) override;

    private:
        bool m_LastRequested {false};

        [[nodiscard]] bool sceneRequestsXR() const;
    };
} // namespace vultra
