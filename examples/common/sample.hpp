#pragma once

#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/desktop_app.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace sample
{
    struct Options
    {
        uint64_t              frames = 0;
        std::filesystem::path capture;
    };

    inline void addCaptureOption(argparse::ArgumentParser& parser)
    {
        parser.add_argument("--capture").help("Save the last frame (first without --frames)");
    }

    inline Options getOptions(const argparse::ArgumentParser& parser)
    {
        return {parser.present<uint64_t>("--frames").value_or(0),
                parser.present<std::string>("--capture").value_or("")};
    }

    inline std::optional<Options> readOptions(int argc, char** argv)
    {
        argparse::ArgumentParser parser(std::filesystem::path(argv[0]).stem().string(),
                                        "0.1.0",
                                        argparse::default_arguments::none);
        parser.add_description("Vultra desktop example");
        vultra::addAppOptions(parser);
        addCaptureOption(parser);
        if (!vultra::parseCommandLine(parser, argc, argv))
        {
            return std::nullopt;
        }
        return getOptions(parser);
    }

    // Called after GPU completion and before presentation. Without --frames, capture the first frame.
    inline void captureFrame(const Options& options, uint64_t frame, vultra::Device& device, vultra::Texture& texture)
    {
        const uint64_t captureIndex = options.frames ? options.frames : 1;
        if (!options.capture.empty() && frame + 1 == captureIndex)
        {
            vultra::savePng(vultra::readback(device, texture), options.capture);
        }
    }
} // namespace sample
