-- "hello" example plugin (pure Lua).
--
-- The PluginSystem runs this file and, on the returned table, calls on_install() at load and
-- on_uninstall() at shutdown. Plugins share the engine's Lua state, so they can register globals
-- that ordinary entity scripts then call, act as a glue layer over a native plugin's bindings,
-- or extend the editor.
local M = {}

function M.on_install()
    print("[hello-plugin] installed")

    -- Expose a helper into the shared Lua state for entity scripts to use.
    function hello_from_plugin(who)
        return "Hello, " .. tostring(who or "world") .. ", from the hello plugin!"
    end
end

function M.on_uninstall()
    print("[hello-plugin] uninstalled")
    hello_from_plugin = nil
end

return M
