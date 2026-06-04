#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/logger.hpp"

#include <cstdlib>
#include <stdexcept>

namespace vultra
{
    class DebugDrawInterface;

    struct CommonContext
    {
        CommonContext();

        void cleanup();

        Logger                  logger;
        // Global immediate-mode debug-draw backend (dd:: library RenderInterface). Created and
        // dd::initialize()'d by RenderSystem; submitters use IRenderService::debugDraw* / dd::.
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

#if defined(__ANDROID__)
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
            throw std::runtime_error("VULTRA_CORE_ASSERT failed: " #expr); \
        } \
    } while (0)
#else
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
#endif
