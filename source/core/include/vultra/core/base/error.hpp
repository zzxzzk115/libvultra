#pragma once

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace vultra
{
    // The one error type of every fallible Vultra operation. The message names the operation and its cause.
    struct Error
    {
        std::string message;
    };

    template<typename... Args>
    std::unexpected<Error> makeError(std::format_string<Args...> format, Args&&... args)
    {
        return std::unexpected(Error {std::format(format, std::forward<Args>(args)...)});
    }

    // Prepends the failed operation to an error returned by a callee.
    inline std::unexpected<Error> withContext(std::string_view operation, const Error& error)
    {
        return std::unexpected(Error {std::format("{}: {}", operation, error.message)});
    }

    // For states no input can produce: logs the message and aborts the process.
    [[noreturn]] void panic(std::string_view message);
} // namespace vultra
