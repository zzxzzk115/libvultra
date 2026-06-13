#include "vultra/function/scripting/bindings/script_coroutine_binding.hpp"

namespace vultra
{
    namespace
    {
        // Pure-Lua coroutine scheduler for entity scripts. Each script
        // environment gets startCoroutine/stopAllCoroutines via bindEnv;
        // ScriptSystem drives tick(dt) once per frame and stops tasks on
        // disable/destroy. wait()/waitFrames() yield wake-up descriptors.
        constexpr const char* kCoroutineRuntime = R"lua(
do
    local tasks = {} -- entityId -> array of { co, waitSeconds, waitFrames }

    local function applyYield(task, yielded)
        if type(yielded) == "table" then
            task.waitSeconds = yielded.seconds
            task.waitFrames = yielded.frames
        else
            task.waitSeconds, task.waitFrames = nil, nil
        end
    end

    local function start(entityId, fn, ...)
        if type(fn) ~= "function" then
            error("startCoroutine expects a function", 2)
        end
        local task = { co = coroutine.create(fn) }
        local list = tasks[entityId]
        if not list then
            list = {}
            tasks[entityId] = list
        end
        list[#list + 1] = task
        local ok, yielded = coroutine.resume(task.co, ...)
        if not ok then
            print("[coroutine] error: " .. tostring(yielded))
        elseif coroutine.status(task.co) ~= "dead" then
            applyYield(task, yielded)
        end
        return task.co
    end

    local function stopAll(entityId) tasks[entityId] = nil end

    local function tick(dt)
        for entityId, list in pairs(tasks) do
            local count = #list
            local write = 1
            for i = 1, count do
                local task = list[i]
                if coroutine.status(task.co) == "dead" then
                    task = nil
                else
                    local due = true
                    if task.waitSeconds and task.waitSeconds > 0 then
                        task.waitSeconds = task.waitSeconds - dt
                        due = task.waitSeconds <= 0
                    elseif task.waitFrames and task.waitFrames > 0 then
                        task.waitFrames = task.waitFrames - 1
                        due = task.waitFrames <= 0
                    end
                    if due then
                        local ok, yielded = coroutine.resume(task.co)
                        if not ok then
                            print("[coroutine] error: " .. tostring(yielded))
                            task = nil
                        elseif coroutine.status(task.co) == "dead" then
                            task = nil
                        else
                            applyYield(task, yielded)
                        end
                    end
                end
                if task then
                    list[write] = task
                    write = write + 1
                end
            end
            for i = write, count do
                list[i] = nil
            end
            if write == 1 then
                tasks[entityId] = nil
            end
        end
    end

    local function bindEnv(env, entityId)
        env.startCoroutine = function(fn, ...) return start(entityId, fn, ...) end
        env.stopAllCoroutines = function() stopAll(entityId) end
    end

    local function reset() tasks = {} end

    __vultraCoroutines = { bindEnv = bindEnv, stopAll = stopAll, tick = tick, reset = reset }
end

-- yield helpers, usable only inside coroutines
function wait(seconds)
    return coroutine.yield({ seconds = seconds or 0 })
end

function waitFrames(frames)
    return coroutine.yield({ frames = frames or 1 })
end

-- global fallbacks so calls outside an entity script fail with a clear message
function startCoroutine(_)
    error("startCoroutine is only available inside entity scripts", 2)
end

function stopAllCoroutines()
    error("stopAllCoroutines is only available inside entity scripts", 2)
end
)lua";
    } // namespace

    void registerScriptCoroutineRuntime(sol::state& lua) { lua.script(kCoroutineRuntime, "@coroutine_runtime"); }
} // namespace vultra
