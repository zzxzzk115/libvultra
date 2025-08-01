#pragma once

#include "vultra/core/base/logger.hpp"

namespace vultra
{
    struct CommonContext
    {
        CommonContext();

        Logger logger;
    };

    extern CommonContext commonContext;

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
} // namespace vultra