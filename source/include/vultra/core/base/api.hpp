#pragma once

#include <vbase/api.hpp>

// ------------------------------------------------------------
// VULTRA_API
// ------------------------------------------------------------
// Build configuration:
// - Define VULTRA_BUILD_SHARED when libvultra is built as a shared library.
// - Define VULTRA_BUILDING_DLL only when compiling libvultra itself.
//
// If you keep libvultra as a static library for now, do NOT define
// VULTRA_BUILD_SHARED and VULTRA_API will be empty.

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
