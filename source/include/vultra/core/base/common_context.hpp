#pragma once

#include "vultra/core/base/logger.hpp"
#include "vultra/function/debug_draw/debug_draw_interface.hpp"

namespace vultra
{
    struct CommonContext
    {
        CommonContext();

        void cleanup();

        Logger                  logger;
        Ref<DebugDrawInterface> debugDraw;
    };

    extern CommonContext commonContext;
} // namespace vultra

#define VULTRA_CORE_TRACE(...) ::vultra::commonContext.logger.trace(true, __VA_ARGS__);
#define VULTRA_CORE_INFO(...) ::vultra::commonContext.logger.info(true, __VA_ARGS__);
#define VULTRA_CORE_WARN(...) ::vultra::commonContext.logger.warn(true, __VA_ARGS__);
#define VULTRA_CORE_ERROR(...) ::vultra::commonContext.logger.error(true, __VA_ARGS__);
#define VULTRA_CORE_CRITICAL(...) ::vultra::commonContext.logger.critical(true, __VA_ARGS__);

#define VULTRA_CLIENT_TRACE(...) ::vultra::commonContext.logger.trace(false, __VA_ARGS__);
#define VULTRA_CLIENT_INFO(...) ::vultra::commonContext.logger.info(false, __VA_ARGS__);
#define VULTRA_CLIENT_WARN(...) ::vultra::commonContext.logger.warn(false, __VA_ARGS__);
#define VULTRA_CLIENT_ERROR(...) ::vultra::commonContext.logger.error(false, __VA_ARGS__);
#define VULTRA_CLIENT_CRITICAL(...) ::vultra::commonContext.logger.critical(false, __VA_ARGS__);

#define VULTRA_CORE_ASSERT(expr, ...) \
    do \
    { \
        if (!(expr)) \
        { \
            if constexpr (sizeof(#__VA_ARGS__) > 1) \
            { \
                VULTRA_CORE_ERROR( \
                    "{}:{}: Assertion '{}' failed. {}", __FILE__, __LINE__, #expr, fmt::format(__VA_ARGS__)); \
            } \
            else \
            { \
                VULTRA_CORE_ERROR("{}:{}: Assertion '{}' failed.", __FILE__, __LINE__, #expr); \
            } \
            DEBUG_BREAK(); \
            assert(expr); \
            std::exit(EXIT_FAILURE); \
        } \
    } while (0)