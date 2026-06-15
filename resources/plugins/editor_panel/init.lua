-- "editor_panel" example plugin (pure Lua).
--
-- Demonstrates the editor extension API (Phase 5): a plugin registers a custom
-- editor panel and a Tools menu item during on_install, and the engine tears
-- them down automatically on unload (registrations are tagged with this
-- plugin's id). The panel body is drawn with the ImGui.* Lua bindings.
--
-- The Editor table only exists in editor/dev builds, so everything is guarded
-- with `if Editor then ... end` -- the same plugin loaded in a shipped runtime
-- simply does nothing.
local M = {}

-- panel state lives in the module so it survives across frames
local state = {
    counter = 0,
    sliderValue = 0.5,
    showGreeting = true,
    message = "Hello from a Lua editor plugin!",
}

function M.on_install()
    if not Editor then
        print("[editor_panel] no editor in this runtime; nothing to register")
        return
    end

    -- A dockable panel. onDraw runs inside the window's Begin/End each frame
    -- and draws with the ImGui bindings (upstream PascalCase names).
    local ok, err = Editor.registerPanel {
        id = "com.vultra.examples.editor_panel.demo",
        title = "Lua Demo Panel",
        defaultOpen = true,
        onDraw = function()
            ImGui.TextWrapped(state.message)
            ImGui.Separator()

            ImGui.Text("Counter: " .. tostring(state.counter))
            if ImGui.Button("Increment") then
                state.counter = state.counter + 1
            end
            ImGui.SameLine()
            if ImGui.Button("Reset") then
                state.counter = 0
            end

            local changed, v = ImGui.SliderFloat("Value", state.sliderValue, 0.0, 1.0)
            if changed then
                state.sliderValue = v
            end

            local toggled, show = ImGui.Checkbox("Show greeting", state.showGreeting)
            if toggled then
                state.showGreeting = show
            end
            if state.showGreeting then
                ImGui.TextColored(Vec4(0.4, 0.9, 0.5, 1.0), "Greeting enabled")
            end

            ImGui.Separator()
            -- Camera.findPrimary returns an Entity; getComponent(Component.Camera)
            -- returns the camera reference (or nil), which exposes the fovY field.
            local cam = Camera.findPrimary()
            local camera = cam and cam.valid and cam:getComponent(Component.Camera)
            if camera then
                ImGui.Text(string.format("Primary camera fovY: %.1f deg", camera.fovY))
            else
                ImGui.TextDisabled("No primary camera in scene")
            end
        end,
        onClosed = function()
            print("[editor_panel] demo panel closed")
        end,
    }
    if not ok then
        print("[editor_panel] registerPanel failed: " .. tostring(err))
    end

    -- A Tools menu entry that opens/focuses nothing of its own -- it just bumps
    -- the panel counter, to show menu -> action wiring. Nested under a submenu
    -- via the "/"-separated path.
    Editor.registerMenuItem {
        id = "com.vultra.examples.editor_panel.bump",
        path = "Lua Examples/Bump Counter",
        shortcut = "",
        onClick = function()
            state.counter = state.counter + 10
            print("[editor_panel] counter bumped to " .. tostring(state.counter))
        end,
        enabledWhen = function()
            return state.showGreeting
        end,
    }

    print("[editor_panel] installed: panel + Tools menu item registered")
end

function M.on_uninstall()
    -- The engine auto-unregisters owned panels/menu items on unload, but doing
    -- it explicitly is harmless and clearer.
    if Editor then
        Editor.unregister("com.vultra.examples.editor_panel.demo")
        Editor.unregister("com.vultra.examples.editor_panel.bump")
    end
    print("[editor_panel] uninstalled")
end

return M
