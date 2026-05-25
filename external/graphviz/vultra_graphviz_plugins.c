#include "config.h"

#include "gvplugin.h"

extern gvplugin_installed_t gvdevice_dot_types[];
extern gvplugin_installed_t gvrender_dot_types[];
extern gvplugin_installed_t gvlayout_dot_layout[];

static gvplugin_api_t vultra_core_apis[] = {
    {API_device, gvdevice_dot_types},
    {API_render, gvrender_dot_types},
    {0, 0},
};

static gvplugin_api_t vultra_dot_layout_apis[] = {
    {API_layout, gvlayout_dot_layout},
    {0, 0},
};

gvplugin_library_t vultra_gvplugin_core_library = {
    "core",
    vultra_core_apis,
};

gvplugin_library_t vultra_gvplugin_dot_layout_library = {
    "dot_layout",
    vultra_dot_layout_apis,
};

