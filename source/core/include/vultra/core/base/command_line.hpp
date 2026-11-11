#pragma once

#include <vultra/core/base/error.hpp>

#include <argparse/argparse.hpp>

#include <expected>

namespace vultra
{
    // Ordinary argparse parser: each application adds its own options before parsing.
    void addAppOptions(argparse::ArgumentParser& parser);
    // Returns false for --help, before creating a window or device. Errors include usage.
    [[nodiscard]] std::expected<bool, Error> parseCommandLine(argparse::ArgumentParser& parser, int argc, char** argv);
} // namespace vultra
