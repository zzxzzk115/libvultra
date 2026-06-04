#pragma once

#include <vbase/service/service_registry.hpp>

#include <entt/entity/fwd.hpp>

#include <string_view>

struct lua_State;

namespace vultra
{
    class IScriptService
    {
    public:
        SERVICE_REGISTER(IScriptService)
        virtual ~IScriptService() = default;

        // Raw Lua state shared by the scripting runtime. Native plugins can wrap this in a
        // sol::state_view to register glue bindings that Lua plugins/scripts then consume.
        virtual lua_State* luaState() = 0;

        virtual bool reloadEntityScript(entt::entity e) = 0;
        virtual bool reloadAllScripts()                 = 0;

        virtual bool hasScriptInstance(entt::entity e) const = 0;
        virtual void destroyScriptInstance(entt::entity e)   = 0;

        virtual void setPlaybackState(bool playing, bool paused) = 0;
        virtual bool isPlaybackPlaying() const                   = 0;
        virtual bool isPlaybackPaused() const                    = 0;
        virtual void requestSingleStep()                         = 0;

        virtual bool runString(std::string_view code) = 0;
    };
} // namespace vultra
