#pragma once

#include <vultra/api/vultra_ui.generated.h>

#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct VultraResearchView
    {
        uint32_t current_method;
        uint32_t active_method;
        uint32_t method_count;
        uint32_t width;
        uint32_t height;
        uint32_t active_width;
        uint32_t active_height;
        uint32_t reference_method;
        float    render_scale;
        int32_t  display;        /* Reference, current, comparison, absolute HDR error. */
        int32_t  headset_output; /* Reference or current. */
        uint8_t  xr;
        uint8_t  reference_snapshot;
        int32_t  preview_eye;     /* All=0, left=1, right=2; presentation only. */
        int32_t  preview_content; /* Result=0, intermediate snapshot=1, FLIP map=2. */
        uint32_t quality_interval;
        uint8_t  live_quality;
        uint8_t  match_viewport;
        uint8_t  vsync; /* Requested desktop mode; independent of headset timing. */
    } VultraResearchView;

    /* Available to on_gui after host initialization. Calls require its live UI frame. */
    typedef struct VultraResearchEditorApi
    {
        uint32_t version;
        uint32_t struct_size;
        void*    context;
        VultraStatus (*get_view)(void*, VultraUiFrame, VultraResearchView*);
        VultraStatus (*set_view)(void*, VultraUiFrame, const VultraResearchView*);
        const char* (*method_name)(void*, VultraUiFrame, uint32_t);
        /* Slot 0 = reference, 1 = current. Editing changes the current configuration only. */
        VultraStatus (*get_parameter)(void*, VultraUiFrame, uint32_t, const char*, const char*, double*);
        VultraStatus (*set_parameter)(void*, VultraUiFrame, const char*, const char*, double);
        VultraStatus (*capture_reference)(void*, VultraUiFrame);
        VultraStatus (*use_rendered_reference)(void*, VultraUiFrame);
        /* Requests one completed-frame assessment, including display/HDR metrics and CPU LDR-FLIP. */
        VultraStatus (*measure)(void*, VultraUiFrame);
        /* Capture a named graph texture after its writer (empty after_pass selects the last writer).
           channel: RGB=0, R=1, G=2, B=3, A=4, luminance=5. Produces a completed-frame viewport snapshot. */
        VultraStatus (*preview)(void*,
                                VultraUiFrame,
                                const char* label,
                                const char* resource,
                                const char* after_pass,
                                uint32_t    channel,
                                float       minimum,
                                float       maximum);
        /* Deferred completed-frame export; refuses an existing directory. all=0 uses visible images. */
        VultraStatus (*save_images)(void*, VultraUiFrame, const char* directory, uint8_t all);
    } VultraResearchEditorApi;
#ifdef __cplusplus
}
#endif
