#pragma once

#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/app_launch.hpp>

#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

namespace sample
{
    struct Mode
    {
        std::string_view name;
        std::string_view description;
        int (*run)(const vultra::AppLaunch&, int, char**);
    };

    inline int runMode(const vultra::AppLaunch& launch, int argc, char** argv, std::span<const Mode> modes)
    {
        const std::string_view requested   = argc > 1 ? argv[1] : "";
        const std::string_view programPath = argv[0];
        const auto             separator   = programPath.find_last_of("/\\");
        const auto             program = programPath.substr(separator == std::string_view::npos ? 0 : separator + 1);
        if (requested.empty() || requested == "--help")
        {
            std::printf("Usage: %.*s <mode> [options]\nModes:\n", int(program.size()), program.data());
            for (const auto& mode : modes)
            {
                std::printf("  %.*s  %.*s\n",
                            int(mode.name.size()),
                            mode.name.data(),
                            int(mode.description.size()),
                            mode.description.data());
            }
            std::printf("Run %.*s <mode> --help for mode options.\n", int(program.size()), program.data());
            return 0;
        }

        const std::string_view name = requested;
        for (const auto& mode : modes)
        {
            if (mode.name != name)
            {
                continue;
            }
            std::vector<char*> forwarded {argv[0]};
            for (int index = 2; index < argc; ++index)
            {
                forwarded.push_back(argv[index]);
            }
            forwarded.push_back(nullptr);
            return mode.run(launch, int(forwarded.size()) - 1, forwarded.data());
        }
        vultra::Logger::app().error("Unknown {} mode: {} (run --help for modes)", program, name);
        return 1;
    }
} // namespace sample
