#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/logger.hpp>

#include <filesystem>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv)
try
{
    if (argc != 3 && argc != 4 && argc != 5)
    {
        throw std::invalid_argument(
            "Usage: vultra-pack <project.vproject> <output.vpk> | --builtins <engine-root> <output.vpk> | "
            "--embed <vultra-runtime> <project.vpk> <output-exe>");
    }
    if (argc == 5 && std::string_view(argv[1]) == "--embed")
    {
        vultra::VpkArchive::embedProject(argv[2], argv[3], argv[4]);
        vultra::Logger::app().info("Embedded project into {}", std::filesystem::path(argv[4]).string());
    }
    else if (argc == 4 && std::string_view(argv[1]) == "--builtins")
    {
        vultra::VpkArchive::packBuiltins(argv[2], argv[3]);
        vultra::Logger::app().info("Packed builtins into {}", std::filesystem::path(argv[3]).string());
    }
    else if (argc == 3)
    {
        vultra::VpkArchive::packProject(argv[1], argv[2]);
        vultra::Logger::app().info("Packed project into {}", std::filesystem::path(argv[2]).string());
    }
    else
    {
        throw std::invalid_argument("Invalid pack arguments");
    }
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
