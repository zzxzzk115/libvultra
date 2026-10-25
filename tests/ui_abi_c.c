#include <vultra/api/vultra_ui.generated.h>

#include <stddef.h>

_Static_assert(sizeof(VultraStatus) == sizeof(int32_t), "Status must retain its 32-bit ABI with short enums");
_Static_assert(offsetof(VultraUiApi, struct_size) == sizeof(uint32_t), "Function-table prefix layout changed");

/* Called by the C++ host during a real GUI frame. This translation unit uses only the public C headers. */
int testUiAbi(const VultraUiApi* api, VultraUiFrame frame)
{
    uint8_t       changed    = 0;
    uint8_t       enabled    = 1;
    int32_t       selection  = 1;
    int32_t       resolution = 640;
    double        tolerance  = 0.1;
    VultraUiFrame stale      = frame;
    VultraUiFrame empty      = {NULL, 0};

    if (api->version != VULTRA_ABI_VERSION || api->struct_size != sizeof(VultraUiApi))
    {
        return __LINE__;
    }
    if (api->header(frame, "Settings", 8, 1, &changed) != VULTRA_STATUS_OK)
    {
        return __LINE__;
    }
    if (api->checkbox(frame, "Enabled", 7, &enabled, &changed) != VULTRA_STATUS_OK || enabled != 1)
    {
        return __LINE__;
    }
    if (api->combo(frame, "Mode", 4, &selection, "Compute\nGraphics", 16, &changed) != VULTRA_STATUS_OK ||
        selection != 1)
    {
        return __LINE__;
    }
    if (api->integerslider(frame, "Width", 5, &resolution, 11, 4096, &changed) != VULTRA_STATUS_OK || resolution != 640)
    {
        return __LINE__;
    }
    if (api->slider(frame, "Tolerance", 9, &tolerance, 0, 1, &changed) != VULTRA_STATUS_OK || tolerance != 0.1)
    {
        return __LINE__;
    }
    if (api->textwrapped(frame, "Help text", 9) != VULTRA_STATUS_OK ||
        api->tooltip(frame, "Tooltip", 7) != VULTRA_STATUS_OK || api->text(frame, NULL, 0) != VULTRA_STATUS_OK)
    {
        return __LINE__;
    }
    if (api->slider(frame, "Null", 4, NULL, 0, 1, &changed) != VULTRA_STATUS_INVALID_ARGUMENT ||
        api->text(frame, NULL, 1) != VULTRA_STATUS_INVALID_ARGUMENT ||
        api->button(frame, "Null", 4, NULL) != VULTRA_STATUS_INVALID_ARGUMENT ||
        api->text(empty, "Empty", 5) != VULTRA_STATUS_INVALID_ARGUMENT)
    {
        return __LINE__;
    }
    enabled   = 2;
    selection = 2;
    if (api->checkbox(frame, "Invalid", 7, &enabled, &changed) != VULTRA_STATUS_INVALID_ARGUMENT ||
        api->combo(frame, "Invalid", 7, &selection, "A\nB", 3, &changed) != VULTRA_STATUS_INVALID_ARGUMENT)
    {
        return __LINE__;
    }
    ++stale.serial;
    if (api->slider(stale, "Expired", 7, &tolerance, 0, 1, &changed) != VULTRA_STATUS_INVALID_FRAME)
    {
        return __LINE__;
    }
#if SIZE_MAX < UINT64_MAX
    if (api->text(frame, "Overflow", (uint64_t)SIZE_MAX + 1) != VULTRA_STATUS_INVALID_ARGUMENT)
    {
        return __LINE__;
    }
#endif
    return 0;
}
