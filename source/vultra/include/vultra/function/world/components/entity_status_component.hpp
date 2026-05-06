#pragma once

namespace vultra
{
    struct EntityStatusComponent
    {
        bool active {true};
        bool visible {true};
        bool locked {false};
        bool selectable {true};
    };
} // namespace vultra
