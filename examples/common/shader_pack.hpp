#pragma once

#include <vultra/app/app_launch.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/error.hpp>
#include <vultra/shader/shader_program.hpp>

#if defined(__ANDROID__)
#include <vultra/platform/android/asset.hpp>

#include <game-activity/native_app_glue/android_native_app_glue.h>
#endif

#include <expected>
#include <optional>
#include <string_view>

namespace sample
{
    // Android and Web load the host-cooked entry from the packaged VPK; desktop compiles the Slang source instead.
    inline std::expected<std::optional<vultra::ShaderProgram>, vultra::Error>
    cookedShader(const vultra::AppLaunch& launch, std::string_view entry)
    {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#if defined(__ANDROID__)
        const auto mapping = vultra::AndroidAsset::open(*launch.activity->activity->assetManager, "project.vpk");
        if (!mapping)
        {
            return std::unexpected(mapping.error());
        }
        const auto pack = vultra::VpkArchive::create("/project.vpk", (*mapping)->bytes());
#else
        static_cast<void>(launch);
        const auto pack = vultra::VpkArchive::create("/project.vpk");
#endif
        if (!pack)
        {
            return std::unexpected(pack.error());
        }
        const auto bytes = pack->read(entry);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        // The program owns its bytecode, so the mapping and archive may close after loading.
        return vultra::ShaderProgram::load(*bytes, entry);
#else
        static_cast<void>(launch);
        static_cast<void>(entry);
        return std::nullopt;
#endif
    }
} // namespace sample
