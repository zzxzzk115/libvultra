#pragma once

#include <argparse/argparse.hpp>

namespace vultra
{
    // Ordinary argparse parser: each application adds its own options before parsing.
    void addAppOptions(argparse::ArgumentParser& parser);
    // Returns false for --help, before creating a window or device. Errors include usage.
    bool parseCommandLine(argparse::ArgumentParser& parser, int argc, char** argv);
} // namespace vultra
