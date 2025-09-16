#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#else
using TracyVkCtx = void*;
#define ZoneScopedN(x)
#define ZoneTransientN(x, y, z)
#define TracyVkZone(x, y, z)
#define TracyVkZoneTransient(a, b, c, d, e)
#define TracyVkDestroy(x)
#define TracyVkZone(x, y, z)
#define TracyVkCollect(x, y)
#define FrameMark
#endif