#pragma once

#include <vultra/app/app_launch.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/error.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace sample
{
    struct Options
    {
        vultra::AppLaunch     launch;
        uint64_t              frames = 0;
        std::filesystem::path capture;
    };

    inline void addCaptureOption(argparse::ArgumentParser& parser)
    {
        parser.add_argument("--capture").help("Save the last frame (first without --frames)");
    }

    inline Options getOptions(const argparse::ArgumentParser& parser, const vultra::AppLaunch& launch = {})
    {
        return {launch,
                parser.present<uint64_t>("--frames").value_or(0),
                parser.present<std::string>("--capture").value_or("")};
    }

    // Empty after --help.
    inline std::expected<std::optional<Options>, vultra::Error>
    readOptions(int argc, char** argv, const vultra::AppLaunch& launch = {})
    {
        argparse::ArgumentParser parser(std::filesystem::path(argv[0]).stem().string(),
                                        "0.1.0",
                                        argparse::default_arguments::none);
        parser.add_description("Vultra graphics example");
        vultra::addAppOptions(parser);
        addCaptureOption(parser);
        const auto parsed = vultra::parseCommandLine(parser, argc, argv);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        if (!*parsed)
        {
            return std::nullopt;
        }
        return getOptions(parser, launch);
    }

    // The exit status of an example that stopped at an error.
    inline int fail(const vultra::Error& error)
    {
        vultra::Logger::app().error("{}", error.message);
        return 1;
    }

    // Called after GPU completion and before presentation. Without --frames, capture the first frame.
    inline std::expected<void, vultra::Error>
    captureFrame(const Options& options, uint64_t frame, vultra::Device& device, vultra::Texture& texture)
    {
        const uint64_t captureIndex = options.frames ? options.frames : 1;
        if (options.capture.empty() || frame + 1 != captureIndex)
        {
            return {};
        }
        const auto image = vultra::readback(device, texture);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        return vultra::savePng(*image, options.capture);
    }
} // namespace sample
