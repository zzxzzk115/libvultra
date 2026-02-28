#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/shader_service.hpp"

namespace vultra
{
    class ShaderSystem final : public EngineSubsystem, public IShaderService
    {
    public:
        ENGINE_SUBSYSTEM(ShaderSystem)

        bool onInit() override;
        void onShutdown() override;

    public:
        rendering::ShaderLibraryRuntime& bulitinLibrary() override { return m_BuiltinShaderLibrary; }

    private:
        rendering::ShaderLibraryRuntime m_BuiltinShaderLibrary;
    };
} // namespace vultra
