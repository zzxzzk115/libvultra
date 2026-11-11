#include <vultra/core/base/error.hpp>
#include <vultra/core/base/logger.hpp>

#include <cstdlib>

namespace vultra
{
    void panic(std::string_view message)
    {
        Logger::core().critical("{}", message);
        Logger::core().flush();
        std::abort();
    }
} // namespace vultra
