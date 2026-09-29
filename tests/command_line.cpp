#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>

#include <chrono>
#include <fstream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool parse(argparse::ArgumentParser& parser, std::vector<std::string> arguments)
    {
        std::vector<char*> argv;
        for (auto& argument : arguments)
        {
            argv.push_back(argument.data());
        }
        return vultra::parseCommandLine(parser, int(argv.size()), argv.data());
    }
} // namespace

int main()
try
{
    const std::vector<std::vector<std::string>> invalid {{"app", "--frames"},
                                                         {"app", "--frames", "-1"},
                                                         {"app", "--frames", "3x"},
                                                         {"app", "--frames", "18446744073709551616"},
                                                         {"app", "--capture"},
                                                         {"app", "--capture", "--frames", "1"},
                                                         {"app", "--log-file"},
                                                         {"app", "--log-level", "invalid"},
                                                         {"app", "--debug", "9"},
                                                         {"app", "--unknown"},
                                                         {"app", "--frames", "1", "--frames", "2"}};
    for (const auto& arguments : invalid)
    {
        argparse::ArgumentParser parser("app", "0.1.0", argparse::default_arguments::none);
        vultra::addAppOptions(parser);
        parser.add_argument("--capture");
        parser.add_argument("--debug").scan<'i', int>().choices(0, 1, 2, 3, 4);
        bool rejected = false;
        try
        {
            parse(parser, arguments);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Invalid or incomplete command line accepted");
    }

    const auto file = std::filesystem::path("build/.tmp/cli-tests") /
                      (std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    argparse::ArgumentParser parser("app", "0.1.0", argparse::default_arguments::none);
    vultra::addAppOptions(parser);
    require(parse(parser, {"app", "--frames", "3", "--log-file", file.string(), "--log-level", "warn"}),
            "Valid command line rejected");
    require(parser.present<uint64_t>("--frames").value_or(0) == 3, "Frame count parse failed");
    vultra::Logger::core().info("hidden-info");
    vultra::Logger::core().warn("core-warning");
    vultra::Logger::app().error("application-error");
    vultra::Logger::core().flush();
    vultra::Logger::app().flush();
    std::ifstream     stream(file);
    const std::string log((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    require(log.find("hidden-info") == std::string::npos && log.find("core-warning") != std::string::npos &&
                log.find("application-error") != std::string::npos,
            "Shared file sink or severity filtering failed");
    vultra::Logger::configure();
    vultra::Logger::app().info(
        "CLI tests passed: numeric bounds, missing values, choices, duplicate options and logging");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
