BindingProbe = {
    elapsed = 0,
    baseY = 0,
    logged = false,
}

local function bool_text(value)
    if value then
        return "true"
    end
    return "false"
end

function OnCreate(self)
    local pos = self.transform.position
    BindingProbe.baseY = pos.y
    BindingProbe.elapsed = 0
    BindingProbe.logged = false

    local text = Asset.loadText("res://scripts/test_move.lua")
    local splat = Asset.loadGaussianSplat("res://models/3dgs/hornedlizard.spz")
    local stats = Asset.memoryStats()
    local overlay = Camera.overlayInfo()
    local gs = Render.getGaussianSplatSettings()

    Render.setGaussianSplatSettings(gs)

    print("[binding_probe] created")
    print("[binding_probe] self.valid=" .. bool_text(self.valid))
    print("[binding_probe] script.playing=" .. bool_text(Script.isPlaybackPlaying()))
    print("[binding_probe] loadText.ok=" .. bool_text(text.ok))
    print("[binding_probe] splat.valid=" .. bool_text(splat.valid) .. " ready=" .. bool_text(splat.ready))
    print("[binding_probe] asset.cpuCacheBytes=" .. tostring(stats.cpuCacheBytes))
    print("[binding_probe] time.frameIndex=" .. tostring(Time.frameIndex()))
    print("[binding_probe] camera.count=" .. tostring(Camera.count()) .. " overlay=" .. tostring(overlay.mode))
    print("[binding_probe] render.clodLevel=" .. tostring(gs.clodLevel))
    print("[binding_probe] xr.enabled=" .. bool_text(RenderBackend.isXREnabled()))
    -- Avoid reloading the active scene during playback; this probe is only meant
    -- to validate binding availability, not trigger a heavy scene load.
    print("[binding_probe] scene.load.package=skipped")
end

function OnUpdate(self, dt)
    BindingProbe.elapsed = BindingProbe.elapsed + dt

    local t = self.transform
    local pos = t.position
    pos.y = BindingProbe.baseY + math.sin(Time.totalTime() * 3.0) * 0.05
    t.position = pos

    if not BindingProbe.logged and BindingProbe.elapsed > 1.0 then
        local mouse = Input.getMousePosition()
        local delta = Input.getMousePositionDelta()
        local renderStats = Render.getGaussianSplatFrameStats()

        print("[binding_probe] script.hasInstance=" .. bool_text(Script.hasInstance(self)))
        print("[binding_probe] fps=" .. tostring(Time.framesPerSecond()))
        print("[binding_probe] mouse=(" .. tostring(mouse.x) .. "," .. tostring(mouse.y) .. ")")
        print("[binding_probe] mouseDelta=(" .. tostring(delta.x) .. "," .. tostring(delta.y) .. ")")
        print("[binding_probe] splat.prepared=" .. tostring(renderStats.preparedSplats))

        BindingProbe.logged = true
    end
end
