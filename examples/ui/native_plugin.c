#include <vultra/api/native_plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PluginState
{
    const VultraUiApi*    ui;
    const VultraSceneApi* scene;
    float                 elapsed;
    uint32_t              updates;
    uint64_t              sceneChildren;
    char                  firstNode[64];
} PluginState;

static VultraStatus update(void* user_data, VultraSceneFrame frame, float delta_seconds)
{
    PluginState* state = (PluginState*)user_data;
    state->elapsed += delta_seconds;
    ++state->updates;
    if (!frame.context)
    {
        return VULTRA_STATUS_OK;
    }
    uint64_t     root   = 0;
    VultraStatus status = state->scene->root_id(frame, &root);
    if (status != VULTRA_STATUS_OK)
    {
        return status;
    }
    status = state->scene->child_count(frame, root, &state->sceneChildren);
    if (status != VULTRA_STATUS_OK || state->sceneChildren == 0)
    {
        return status;
    }
    uint64_t first = 0;
    status         = state->scene->child_id(frame, root, 0, &first);
    if (status != VULTRA_STATUS_OK)
    {
        return status;
    }
    const char* name = NULL;
    uint64_t    size = 0;
    status           = state->scene->node_name(frame, first, &name, &size);
    if (status != VULTRA_STATUS_OK)
    {
        return status;
    }
    const size_t length = size < sizeof(state->firstNode) - 1 ? (size_t)size : sizeof(state->firstNode) - 1;
    memcpy(state->firstNode, name, length);
    state->firstNode[length] = '\0';
    return VULTRA_STATUS_OK;
}

static VultraStatus on_gui(void* user_data, VultraUiFrame frame)
{
    PluginState* state = (PluginState*)user_data;
    char         message[96];
    int size = snprintf(message, sizeof(message), "Native plugin: %u updates, %.2f s", state->updates, state->elapsed);
    if (size < 0 || size >= (int)sizeof(message))
    {
        return VULTRA_STATUS_ERROR;
    }
    VultraStatus status = state->ui->text(frame, message, (uint64_t)size);
    if (status != VULTRA_STATUS_OK)
    {
        return status;
    }
    if (state->firstNode[0])
    {
        size = snprintf(message,
                        sizeof(message),
                        "Scene: %llu children; first: %s",
                        (unsigned long long)state->sceneChildren,
                        state->firstNode);
        if (size < 0 || size >= (int)sizeof(message))
        {
            return VULTRA_STATUS_ERROR;
        }
        status = state->ui->text(frame, message, (uint64_t)size);
        if (status != VULTRA_STATUS_OK)
        {
            return status;
        }
    }
    uint8_t clicked = 0;
    status          = state->ui->button(frame, "Reset plugin clock", strlen("Reset plugin clock"), &clicked);
    if (clicked)
    {
        state->elapsed = 0;
    }
    return status;
}

static VultraStatus stop(void* user_data)
{
    free(user_data);
    return VULTRA_STATUS_OK;
}

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
VultraStatus vultra_plugin_init(const VultraHostApi* host, VultraPluginApi* plugin)
{
    if (!host || !plugin || host->version != VULTRA_ABI_VERSION || host->struct_size < sizeof(VultraHostApi) ||
        !host->ui || host->ui->version != VULTRA_ABI_VERSION || host->ui->struct_size < sizeof(VultraUiApi) ||
        !host->scene || host->scene->version != VULTRA_ABI_VERSION ||
        host->scene->struct_size < sizeof(VultraSceneApi) || plugin->version != VULTRA_ABI_VERSION ||
        plugin->struct_size < sizeof(VultraPluginApi))
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    PluginState* state = (PluginState*)calloc(1, sizeof(PluginState));
    if (!state)
    {
        return VULTRA_STATUS_ERROR;
    }
    state->ui         = host->ui;
    state->scene      = host->scene;
    plugin->user_data = state;
    plugin->update    = update;
    plugin->on_gui    = on_gui;
    plugin->stop      = stop;
    return VULTRA_STATUS_OK;
}
