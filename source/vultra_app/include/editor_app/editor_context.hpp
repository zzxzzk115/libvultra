#pragma once

#include "app_state.hpp"

#include <vbase/service/service_registry.hpp>

namespace vultra_app
{
    struct EditorContext
    {
        AppState&               state;
        vbase::ServiceRegistry* services {nullptr};
    };
} // namespace vultra_app
