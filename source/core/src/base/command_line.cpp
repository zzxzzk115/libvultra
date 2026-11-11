#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace vultra
{
    void addAppOptions(argparse::ArgumentParser& parser)
    {
        parser.add_argument("-h", "--help").flag().help("Show this help and exit");
        parser.add_argument("--frames").scan<'u', uint64_t>().help("Stop after N frames (0: until closed)");
        parser.add_argument("--log-level").choices("trace", "debug", "info", "warn", "error", "critical", "off");
        parser.add_argument("--log-file").help("Append logs to this file");
    }

    std::expected<bool, Error> parseCommandLine(argparse::ArgumentParser& parser, int argc, char** argv)
    {
        // argparse checks required positionals before our manual help flag.
        if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h"))
        {
            std::cout << parser;
            return false;
        }
        // argparse reports parse errors only by throwing std::runtime_error (including std::range_error) or
        // std::invalid_argument; its std::logic_error throws are API misuse. The exact types are named because on
        // Apple the -fno-rtti openxr loader brings hidden std::exception/std::logic_error typeinfos that defeat
        // matching through those bases.
        std::string failure;
        try
        {
            parser.parse_args(argc, argv);
        }
        catch (const std::runtime_error& error)
        {
            failure = error.what();
        }
        catch (const std::invalid_argument& error)
        {
            failure = error.what();
        }
        if (!failure.empty())
        {
            std::ostringstream message;
            message << failure << '\n' << parser;
            return std::unexpected(Error {message.str()});
        }
        if (parser.get<bool>("--help"))
        {
            std::cout << parser;
            return false;
        }
        if (auto result =
                Logger::configure(spdlog::level::from_str(parser.present<std::string>("--log-level").value_or("info")),
                                  parser.present<std::string>("--log-file").value_or(""));
            !result)
        {
            return std::unexpected(result.error());
        }
        return true;
    }
} // namespace vultra
