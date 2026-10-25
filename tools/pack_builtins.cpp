#include <vultra/assets/builtin_pack.hpp>
#include <vultra/core/base/logger.hpp>

#include <stdexcept>

int main(int argc, char** argv)
try
{
    if (argc != 3)
    {
        throw std::invalid_argument("Usage: vultra-builtins library-root output.vpk");
    }
    vultra::packBuiltins(argv[1], argv[2]);
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
