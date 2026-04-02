#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <vulkan/vulkan.hpp>
#include <tracy/TracyVulkan.hpp>
using TracyGpuContext = TracyVkCtx;
#define TracyGpuZone TracyVkZone
#define TracyGpuZoneTransient TracyVkZoneTransient
#define TracyGpuDestroy TracyVkDestroy
#define TracyGpuCollect TracyVkCollect
#else
using TracyGpuContext = void*;
#define ZoneScopedN(x)
#define ZoneTransientN(x, y, z)
#define TracyGpuZone(x, y, z)
#define TracyGpuZoneTransient(a, b, c, d, e)
#define TracyGpuDestroy(x)
#define TracyGpuCollect(x, y)
#define FrameMark
#endif
