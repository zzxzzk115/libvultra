#pragma once

#include <vultra/api/vultra_scene.generated.h>
#include <vultra/api/vultra_ui.generated.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct VultraResearchApi VultraResearchApi;

    typedef struct VultraScriptRegistrationApi
    {
        uint32_t version;
        uint32_t struct_size;
        void*    user_data;
        VultraStatus (*register_class)(void* user_data, const char* name, uint64_t name_size);
    } VultraScriptRegistrationApi;

    typedef struct VultraHostApi
    {
        uint32_t              version;
        uint32_t              struct_size;
        const VultraUiApi*    ui;
        const VultraSceneApi* scene;
        /* Available only while initializing a project extension. */
        const VultraScriptRegistrationApi* scripts;
        const VultraResearchApi*           research;
    } VultraHostApi;

    typedef struct VultraPluginApi
    {
        uint32_t version;
        uint32_t struct_size;
        void*    user_data;
        VultraStatus (*update)(void* user_data, VultraSceneFrame scene, float delta_seconds);
        VultraStatus (*on_gui)(void* user_data, VultraUiFrame frame);
        VultraStatus (*stop)(void* user_data);
    } VultraPluginApi;

    /* The only symbol the host looks up in a native plugin. */
    typedef VultraStatus (*VultraPluginInit)(const VultraHostApi* host, VultraPluginApi* plugin);

#ifdef __cplusplus
}
#endif
