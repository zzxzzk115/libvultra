#include <vultra/core/base/logger.hpp>
#include <vultra/shader/builtin_pack.hpp>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        vultra::Logger::app().error("Usage: vultra-builtins library-root output.vpk");
        return 1;
    }
    if (auto packed = vultra::packBuiltins(argv[1], argv[2]); !packed)
    {
        vultra::Logger::app().error("{}", packed.error().message);
        return 1;
    }
    return 0;
}
