#pragma once

#include <vbase/api.hpp>

#if defined(VULTRA_BUILD_SHARED)
    #if defined(VULTRA_BUILDING_DLL)
        #define VULTRA_API VBASE_DLL_EXPORT
    #else
        #define VULTRA_API VBASE_DLL_IMPORT
    #endif
#else
    #define VULTRA_API
#endif

// C ABI exports for plugin entry points
#define VULTRA_PLUGIN_API extern "C" VULTRA_API
