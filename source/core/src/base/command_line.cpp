#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>

#include <iostream>
#include <sstream>

namespace vultra
{
    void addAppOptions(argparse::ArgumentParser& parser)
    {
        parser.add_argument("-h", "--help").flag().help("Show this help and exit");
        parser.add_argument("--frames").scan<'u', uint64_t>().help("Stop after N frames (0: until closed)");
        parser.add_argument("--log-level").choices("trace", "debug", "info", "warn", "error", "critical", "off");
        parser.add_argument("--log-file").help("Append logs to this file");
    }

    bool parseCommandLine(argparse::ArgumentParser& parser, int argc, char** argv)
    {
        try
        {
            parser.parse_args(argc, argv);
        }
        catch (const std::exception& error)
        {
            std::ostringstream message;
            message << error.what() << '\n' << parser;
            throw std::invalid_argument(message.str());
        }
        if (parser.get<bool>("--help"))
        {
            std::cout << parser;
            return false;
        }
        Logger::configure(spdlog::level::from_str(parser.present<std::string>("--log-level").value_or("info")),
                          parser.present<std::string>("--log-file").value_or(""));
        return true;
    }
} // namespace vultra
