#pragma once

#include <vultra/core/base/error.hpp>

#include <spdlog/spdlog.h>

#include <expected>
#include <filesystem>

namespace vultra
{
    // Two named spdlog loggers, sharing console and optional file sinks. No logging macros.
    class Logger
    {
    public:
        [[nodiscard]] static std::expected<void, Error> configure(spdlog::level::level_enum level = spdlog::level::info,
                                                                  const std::filesystem::path& file = {});
        static spdlog::logger&                          core();
        static spdlog::logger&                          app();
    };
} // namespace vultra
