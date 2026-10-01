#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>

namespace vultra
{
    // Two named spdlog loggers, sharing console and optional file sinks. No logging macros.
    class Logger
    {
    public:
        static void            configure(spdlog::level::level_enum    level = spdlog::level::info,
                                         const std::filesystem::path& file  = {});
        static spdlog::logger& core();
        static spdlog::logger& app();
    };
} // namespace vultra
