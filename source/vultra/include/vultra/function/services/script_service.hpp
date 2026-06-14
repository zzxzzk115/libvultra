#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include <vbase/service/service_registry.hpp>

#include <entt/entity/fwd.hpp>

#include <string_view>

struct lua_State;

namespace vultra
{
    // Lua namespace `Script` (doc/lua_api_design.md), generated from these
    // VBIND_FN annotations. Entity-taking wrappers null-check the service and
    // ctx.isValid(entity) before dispatching.
    class VBIND_MODULE(name = Script, service = scriptService) IScriptService
    {
    public:
        SERVICE_REGISTER(IScriptService)
        virtual ~IScriptService() = default;

        // Raw Lua state shared by the scripting runtime. Native plugins can wrap this in a
        // sol::state_view to register glue bindings that Lua plugins/scripts then consume.
        virtual lua_State* luaState() = 0;

        VBIND_FN(name = reloadEntity) virtual bool reloadEntityScript(entt::entity e) = 0;
        VBIND_FN(name = reloadAll) virtual bool reloadAllScripts()                    = 0;

        VBIND_FN(name = hasInstance) virtual bool hasScriptInstance(entt::entity e) const = 0;
        VBIND_FN(name = destroyInstance) virtual void destroyScriptInstance(entt::entity e) = 0;

        VBIND_FN() virtual void setPlaybackState(bool playing, bool paused) = 0;
        VBIND_FN() virtual bool isPlaybackPlaying() const                   = 0;
        VBIND_FN() virtual bool isPlaybackPaused() const                    = 0;
        virtual void requestSingleStep()                                    = 0;

        VBIND_FN() virtual bool runString(std::string_view code) = 0;
    };
} // namespace vultra
