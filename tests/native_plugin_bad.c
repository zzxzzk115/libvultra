#include <vultra/api/native_plugin.h>

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
VultraStatus vultra_plugin_init(const VultraHostApi* host, VultraPluginApi* plugin)
{
    if (!host || !plugin)
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    plugin->version = VULTRA_ABI_VERSION + 1;
    return VULTRA_STATUS_OK;
}
