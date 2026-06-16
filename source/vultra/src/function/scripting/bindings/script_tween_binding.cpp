#include "vultra/function/scripting/bindings/script_tween_binding.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    namespace
    {
        // Pure-Lua tween/timer/timeline scheduler. Tasks are global step closures advanced by
        // __vultraTween.tick(dt) (driven by ScriptSystem each frame). Easing functions map a
        // normalized t in [0,1] to an eased [0,1]. Handles returned by Tween.to / Timer.* /
        // Timeline:start can be cancelled via the matching cancel().
        constexpr const char* kTweenRuntime = R"lua(
do
    local tasks = {}
    local nextId = 1

    local function addTask(step)
        local id = nextId
        nextId = nextId + 1
        tasks[id] = step
        return id
    end

    local function cancel(id)
        if id ~= nil then tasks[id] = nil end
    end

    local function tick(dt)
        for id, step in pairs(tasks) do
            local ok, done = pcall(step, dt)
            if not ok then
                print("[tween] error: " .. tostring(done))
                tasks[id] = nil
            elseif done then
                tasks[id] = nil
            end
        end
    end

    local function reset() tasks = {}; nextId = 1 end

    local pi = math.pi
    Ease = {
        linear     = function(t) return t end,
        quadIn     = function(t) return t * t end,
        quadOut    = function(t) return t * (2 - t) end,
        quadInOut  = function(t) if t < 0.5 then return 2 * t * t else return -1 + (4 - 2 * t) * t end end,
        cubicIn    = function(t) return t * t * t end,
        cubicOut   = function(t) local f = t - 1; return f * f * f + 1 end,
        cubicInOut = function(t) if t < 0.5 then return 4 * t * t * t else local f = 2 * t - 2; return 0.5 * f * f * f + 1 end end,
        sineIn     = function(t) return 1 - math.cos(t * pi / 2) end,
        sineOut    = function(t) return math.sin(t * pi / 2) end,
        sineInOut  = function(t) return -(math.cos(pi * t) - 1) / 2 end,
        expoIn     = function(t) if t <= 0 then return 0 else return 2 ^ (10 * (t - 1)) end end,
        expoOut    = function(t) if t >= 1 then return 1 else return 1 - 2 ^ (-10 * t) end end,
        backOut    = function(t) local c1 = 1.70158; local c3 = c1 + 1; local f = t - 1; return 1 + c3 * f * f * f + c1 * f * f end,
        bounceOut  = function(t)
            local n1, d1 = 7.5625, 2.75
            if t < 1 / d1 then return n1 * t * t
            elseif t < 2 / d1 then t = t - 1.5 / d1; return n1 * t * t + 0.75
            elseif t < 2.5 / d1 then t = t - 2.25 / d1; return n1 * t * t + 0.9375
            else t = t - 2.625 / d1; return n1 * t * t + 0.984375 end
        end,
    }

    -- Tween.to(obj, toFields, duration, opts): interpolate obj's numeric fields toward
    -- toFields over duration seconds. opts = { ease = Ease.*, onUpdate = function(t),
    -- onComplete = function() }. Returns a handle for Tween.cancel.
    Tween = {}
    function Tween.to(obj, toFields, duration, opts)
        opts = opts or {}
        local ease       = opts.ease or Ease.linear
        local onComplete = opts.onComplete
        local onUpdate   = opts.onUpdate
        local from = {}
        for k, _ in pairs(toFields) do from[k] = obj[k] or 0 end
        local elapsed = 0
        duration = math.max(duration or 0, 0)
        return addTask(function(dt)
            elapsed = elapsed + dt
            local t = duration > 0 and math.min(elapsed / duration, 1) or 1
            local e = ease(t)
            for k, target in pairs(toFields) do
                obj[k] = from[k] + (target - from[k]) * e
            end
            if onUpdate then onUpdate(t) end
            if t >= 1 then
                if onComplete then onComplete() end
                return true
            end
            return false
        end)
    end
    function Tween.cancel(handle) cancel(handle) end

    -- Timer.after(seconds, fn): fire fn once after seconds. Returns a handle.
    -- Timer.every(seconds, fn): fire fn every seconds; fn returning false stops it.
    Timer = {}
    function Timer.after(seconds, fn)
        local remaining = math.max(seconds or 0, 0)
        return addTask(function(dt)
            remaining = remaining - dt
            if remaining <= 0 then fn(); return true end
            return false
        end)
    end
    function Timer.every(seconds, fn)
        local interval  = math.max(seconds or 0, 0)
        local remaining = interval
        return addTask(function(dt)
            remaining = remaining - dt
            while remaining <= 0 do
                if fn() == false then return true end
                if interval <= 0 then return false end
                remaining = remaining + interval
            end
            return false
        end)
    end
    function Timer.cancel(handle) cancel(handle) end

    -- Timeline: ordered sequence of waits + calls. Timeline.new():wait(s):call(fn):start().
    Timeline = {}
    Timeline.__index = Timeline
    function Timeline.new() return setmetatable({ steps = {} }, Timeline) end
    function Timeline:wait(seconds)
        self.steps[#self.steps + 1] = { kind = "wait", seconds = math.max(seconds or 0, 0) }
        return self
    end
    function Timeline:call(fn)
        self.steps[#self.steps + 1] = { kind = "call", fn = fn }
        return self
    end
    function Timeline:start()
        local i       = 1
        local waiting = 0
        local steps   = self.steps
        return addTask(function(dt)
            if waiting > 0 then
                waiting = waiting - dt
                if waiting > 0 then return false end
            end
            while i <= #steps do
                local s = steps[i]
                i = i + 1
                if s.kind == "wait" then
                    waiting = s.seconds
                    if waiting > 0 then return false end
                else
                    s.fn()
                end
            end
            return true
        end)
    end

    __vultraTween = { tick = tick, reset = reset }
end
)lua";
    } // namespace

    void registerScriptTweenRuntime(sol::state& lua) { lua.script(kTweenRuntime, "@tween_runtime"); }
} // namespace vultra
