#pragma once

union SDL_Event;

namespace vultra
{
    class Input;

    namespace platform
    {
        void processSdlInput(Input& input, const SDL_Event& event);
    } // namespace platform
} // namespace vultra
