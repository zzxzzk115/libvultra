#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/core/base/logger.hpp>

#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace support
{
    // A test stops at its first failed requirement and exits with status 1.
    [[noreturn]] inline void fail(std::string_view message)
    {
        vultra::Logger::app().error("{}", message);
        vultra::Logger::app().flush();
        std::exit(1);
    }

    inline void require(bool condition, std::string_view message)
    {
        if (!condition)
        {
            fail(message);
        }
    }

    // The value of an operation the test expects to succeed.
    template<typename T>
    T must(std::expected<T, vultra::Error> result)
    {
        if (!result)
        {
            fail(result.error().message);
        }
        return std::move(*result);
    }

    inline void must(const std::expected<void, vultra::Error>& result)
    {
        if (!result)
        {
            fail(result.error().message);
        }
    }

    // Checks an operation the test expects to be rejected; optionally the message must contain `cause`.
    template<typename T>
    void rejects(const std::expected<T, vultra::Error>& result, std::string_view what, std::string_view cause = {})
    {
        if (result)
        {
            fail(what);
        }
        if (!cause.empty() && result.error().message.find(cause) == std::string_view::npos)
        {
            fail(std::string(what) + ": unexpected error: " + result.error().message);
        }
    }
} // namespace support
