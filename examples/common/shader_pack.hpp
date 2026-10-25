#pragma once

#include <vultra/assets/vpk_archive.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/main/app/app_launch.hpp>

#if defined(__ANDROID__)
#include <vultra/platform/android/asset.hpp>

#include <game-activity/native_app_glue/android_native_app_glue.h>
#endif

#include <optional>
#include <string_view>

namespace sample
{
    inline std::optional<vultra::ShaderProgram> cookedShader(const vultra::AppLaunch& launch, std::string_view entry)
    {
#if defined(__ANDROID__)
        vultra::AndroidAsset mapping(*launch.activity->activity->assetManager, "project.vpk");
        vultra::VpkArchive   pack("/project.vpk", mapping.bytes());
        return vultra::ShaderProgram::load(pack.read(entry), entry);
#elif defined(__EMSCRIPTEN__)
        static_cast<void>(launch);
        vultra::VpkArchive pack("/project.vpk");
        return vultra::ShaderProgram::load(pack.read(entry), entry);
#else
        static_cast<void>(launch);
        static_cast<void>(entry);
        return std::nullopt;
#endif
    }
} // namespace sample
