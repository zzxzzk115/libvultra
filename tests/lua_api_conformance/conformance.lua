-- Lua API conformance checker. Run by tests/lua_api_conformance/main.cpp.
-- Rules: doc/lua_api_design.md. Returns the number of failures (0 = pass).
--
-- Violation ids are "<rule>:<symbol>". Pre-existing violations live in
-- exceptions.lua (the burn-down baseline): a violation NOT in the baseline
-- fails the run, and a baseline entry that no longer reproduces fails too
-- (the baseline may only shrink, never rot).
--
-- Limitations (by design, documented):
--  * sol2 usertype instance members (properties/methods) are not reliably
--    enumerable from Lua, so member coverage of usertypes is best-effort:
--    stub @field entries for usertypes are not stale-checked.
--  * Property-vs-method and boolean return types cannot be introspected;
--    those spec rules are enforced by review checklist, not mechanically.

local Conf = Conformance

-- ---------------------------------------------------------------- helpers

local function isPascalCase(name)
    return string.match(name, "^[A-Z][A-Za-z0-9]*$") ~= nil
end

local function isCamelCase(name)
    return string.match(name, "^[a-z][A-Za-z0-9]*$") ~= nil
end

local violations = {} -- id -> true
local function violate(rule, symbol)
    violations[rule .. ":" .. symbol] = true
end

local hardFailures = {}
local function hardFail(msg)
    table.insert(hardFailures, msg)
end

local function sortedKeys(t)
    local keys = {}
    for k in pairs(t) do
        table.insert(keys, k)
    end
    table.sort(keys)
    return keys
end

-- ------------------------------------------------- collect the live surface

local baseline = {}
for _, name in ipairs(Conf.baselineGlobals) do
    baseline[name] = true
end
baseline["Conformance"] = true

local roots = {} -- name -> value
for name, value in pairs(_G) do
    -- "sol."-prefixed globals are sol2 usertype registry internals;
    -- "__"-prefixed globals are engine machinery (deprecation registry etc.)
    if type(name) == "string" and not baseline[name] and not string.match(name, "^sol%.")
        and not string.match(name, "^__") then
        roots[name] = value
    end
end

-- old names kept alive by the deprecation shim are exempt from naming and
-- stub-coverage rules; the shim registry is the authoritative list
local deprecated = type(__vultraDeprecated) == "table" and __vultraDeprecated or {}

-- conditionally-registered tables: absent in headless runs (stale-stub check
-- skipped), and spec-exempt from naming rules when present (upstream names;
-- doc/lua_api_design.md section 9)
local conditionalTops = { ImGui = true }

-- sol2 injects these helpers into every usertype table; they are machinery,
-- not API surface
local sol2InternalMembers = {
    class_cast  = true,
    class_check = true,
    new         = true,
}

-- surface[symbol] = kind; symbols are "Root" or "Root.member"
local surface = {}

local function classifyTable(t)
    -- enum tables hold only numeric values; sol2 usertype tables may not be
    -- iterable at all (pairs raises), in which case they are usertypes
    local kind
    local ok = pcall(function()
        local sawAny = false
        for _, v in pairs(t) do
            sawAny = true
            if type(v) ~= "number" then
                kind = "namespace"
                return
            end
        end
        kind = sawAny and "enum" or "namespace"
    end)
    if not ok then
        return "usertype"
    end
    return kind
end

local function collectMembers(rootName, value)
    local members = {}
    local ok = pcall(function()
        for k, v in pairs(value) do
            if type(k) == "string" and not string.match(k, "^__") then
                members[k] = type(v)
            end
        end
    end)
    if not ok then
        local mt = getmetatable(value)
        if type(mt) == "table" then
            pcall(function()
                for k, v in pairs(mt) do
                    if type(k) == "string" and not string.match(k, "^__") then
                        members[k] = type(v)
                    end
                end
            end)
        end
    end
    return members
end

for rootName, value in pairs(roots) do
    local valueType = type(value)
    if conditionalTops[rootName] then
        surface[rootName] = "conditional" -- present; members keep upstream names
    elseif valueType == "function" then
        surface[rootName] = "function"
        if not isCamelCase(rootName) then
            violate("naming.camelCase", rootName)
        end
    elseif valueType == "table" or valueType == "userdata" then
        local kind = valueType == "table" and classifyTable(value) or "usertype"
        surface[rootName] = kind
        if not isPascalCase(rootName) then
            violate("naming.pascalCase", rootName)
        end
        local memberCase = (kind == "enum") and isPascalCase or isCamelCase
        local memberRule = (kind == "enum") and "naming.pascalCase" or "naming.camelCase"
        local members = collectMembers(rootName, value)
        for memberName in pairs(members) do
            if not sol2InternalMembers[memberName] then
                local symbol = rootName .. "." .. memberName
                surface[symbol] = "member"
                if not deprecated[symbol] then
                    if not memberCase(memberName) then
                        violate(memberRule, symbol)
                    end
                    -- getX is allowed when a matching setX sibling exists:
                    -- parameterized symmetric pairs (Animation.getFloat/
                    -- setFloat) are spec-legal (doc/lua_api_design.md sec. 1)
                    local suffix = string.match(memberName, "^get([A-Z].*)$")
                    if suffix and members["set" .. suffix] == nil then
                        violate("naming.getPrefix", symbol)
                    end
                    if string.match(memberName, "Euler$") or string.match(memberName, "Degrees$") then
                        violate("naming.unitSuffix", symbol)
                    end
                end
            end
        end
    else
        surface[rootName] = valueType
        violate("naming.unexpectedGlobal", rootName)
    end
end

-- ------------------------------------------------------------ parse the stub

local stubTop = {}     -- top-level symbol -> true
local stubMembers = {} -- "Class.member" -> true

do
    local currentClass = nil
    local file = assert(io.open(Conf.stubPath, "r"), "cannot open stub: " .. tostring(Conf.stubPath))
    for line in file:lines() do
        local cls = string.match(line, "^%-%-%-@class%s+([%w_]+)")
        if cls then
            currentClass = cls
            stubTop[cls] = true
        end
        local fld = string.match(line, "^%-%-%-@field%s+([%w_]+)")
        if fld and currentClass then
            stubMembers[currentClass .. "." .. fld] = true
        end
        local owner, member = string.match(line, "^function%s+([%w_]+)[%.:]([%w_]+)%s*%(")
        if owner and member then
            stubTop[owner] = true
            stubMembers[owner .. "." .. member] = true
        else
            local fn = string.match(line, "^function%s+([%w_]+)%s*%(")
            if fn then
                stubTop[fn] = true
            end
        end
    end
    file:close()
end

-- stale stub entries are hard failures: the stub must never describe API that
-- does not exist.
for _, sym in ipairs(sortedKeys(stubTop)) do
    if rawget(_G, sym) == nil and not conditionalTops[sym] then
        hardFail("stub.stale:" .. sym .. " (stub describes a global that is not bound)")
    end
end
for _, sym in ipairs(sortedKeys(stubMembers)) do
    local owner, member = string.match(sym, "^([%w_]+)%.([%w_]+)$")
    local ownerValue = owner and rawget(_G, owner)
    if owner and conditionalTops[owner] and ownerValue == nil then
        ownerValue = nil -- conditional table absent in this run; skip
    elseif type(ownerValue) == "table" and getmetatable(ownerValue) == nil then
        -- plain namespace/enum table: members are directly indexable
        if ownerValue[member] == nil then
            hardFail("stub.stale:" .. sym .. " (stub describes a member that is not bound)")
        end
    end
    -- usertypes: instance members are not statically indexable; skipped.
end

-- live symbols missing from the stub: violations (burned down via baseline);
-- deprecated aliases are exempt (stub documents canonical names)
for _, sym in ipairs(sortedKeys(surface)) do
    local covered = stubTop[sym] or stubMembers[sym] or deprecated[sym]
    if not covered then
        violate("stub.missing", sym)
    end
end

-- ------------------------------------- deprecation shim behavior asserts
-- Skipped in dump mode: exercising deprecated aliases logs warnings to
-- stdout, which would pollute the generated exceptions.lua baseline.

if not Conf.dump then
    local direct = Vec3(4, 5, 6) -- the canonical call-constructor form
    if not (direct and direct.x == 4 and direct.z == 6) then
        hardFail("shim.broken:Vec3(...) call constructor does not work")
    end
    local v = vec3(1, 2, 3) -- deprecated alias must forward to Vec3(...)
    if not (v and v.x == 1 and v.y == 2 and v.z == 3) then
        hardFail("shim.broken:vec3 alias does not forward to Vec3 constructor")
    end
    if type(Input) == "table" then
        if type(Input.isKeyHeld) ~= "function" then
            hardFail("shim.broken:Input.isKeyHeld canonical function missing")
        end
        if type(Input.getKey) ~= "function" then
            hardFail("shim.broken:Input.getKey deprecated alias missing")
        end
    end
    if deprecated["Input.getKey"] ~= "Input.isKeyHeld" then
        hardFail("shim.broken:__vultraDeprecated registry missing Input.getKey")
    end
end

-- ------------------------------------ coroutine runtime behavior asserts

do
    if type(__vultraCoroutines) ~= "table" then
        hardFail("coroutines.broken:__vultraCoroutines runtime missing")
    else
        local env = {}
        __vultraCoroutines.bindEnv(env, 999999)
        local steps = {}
        env.startCoroutine(function(tag)
            steps[#steps + 1] = "start:" .. tostring(tag)
            wait(0.05)
            steps[#steps + 1] = "afterWait"
            waitFrames(2)
            steps[#steps + 1] = "afterFrames"
        end, "abc")
        if steps[1] ~= "start:abc" then
            hardFail("coroutines.broken:first slice did not run on startCoroutine")
        end
        __vultraCoroutines.tick(0.1) -- satisfies wait(0.05)
        if steps[2] ~= "afterWait" then
            hardFail("coroutines.broken:wait(seconds) did not resume")
        end
        __vultraCoroutines.tick(0.0) -- frame 1 of 2
        __vultraCoroutines.tick(0.0) -- frame 2 of 2
        if steps[3] ~= "afterFrames" then
            hardFail("coroutines.broken:waitFrames did not resume after 2 ticks")
        end
        -- stopAll drops pending tasks
        local cancelled = false
        env.startCoroutine(function()
            wait(1)
            cancelled = true
        end)
        env.stopAllCoroutines()
        __vultraCoroutines.tick(10.0)
        if cancelled then
            hardFail("coroutines.broken:stopAllCoroutines did not cancel tasks")
        end
        -- global fallbacks must raise outside entity scripts
        local ok = pcall(startCoroutine, function() end)
        if ok then
            hardFail("coroutines.broken:global startCoroutine fallback should error")
        end
    end
end

-- ----------------------------------------- ImGui binding behavior asserts

if type(ImGui) == "table" then
    if type(ImGui.Begin) ~= "function" or type(ImGui.Button) ~= "function" then
        hardFail("imgui.broken:generated functions missing from ImGui table")
    end
    if not (type(ImGui.WindowFlags) == "table" and ImGui.WindowFlags.NoTitleBar == 1) then
        hardFail("imgui.broken:enum tables missing or wrong values")
    end
    -- out-of-frame guard: calling without an active ImGui frame must raise
    local ok, err = pcall(ImGui.Begin, "conformance")
    if ok or not string.find(tostring(err), "ImGui frame", 1, true) then
        hardFail("imgui.broken:out-of-frame guard did not raise (" .. tostring(err) .. ")")
    end
end

-- ------------------------------------------------------------- dump mode

if Conf.dump then
    print("-- Lua API conformance burn-down baseline. Generated by")
    print("-- test-lua-api-conformance --dump. May only shrink; see")
    print("-- doc/lua_api_design.md.")
    print("return {")
    for _, id in ipairs(sortedKeys(violations)) do
        print(string.format("    [%q] = true,", id))
    end
    print("}")
    return 0
end

-- ------------------------------------------------- compare against baseline

local okExceptions, exceptions = pcall(dofile, Conf.exceptionsPath)
if not okExceptions or type(exceptions) ~= "table" then
    hardFail("exceptions.lua missing or invalid: " .. tostring(exceptions))
    exceptions = {}
end

local newViolations = {}
for id in pairs(violations) do
    if not exceptions[id] then
        table.insert(newViolations, id)
    end
end
table.sort(newViolations)

local staleExceptions = {}
for id in pairs(exceptions) do
    if not violations[id] then
        table.insert(staleExceptions, id)
    end
end
table.sort(staleExceptions)

-- ------------------------------------------------------------------ report

local rootCount, memberCount, violationCount, exceptionCount = 0, 0, 0, 0
for sym, kind in pairs(surface) do
    if kind == "member" then
        memberCount = memberCount + 1
    else
        rootCount = rootCount + 1
    end
end
for _ in pairs(violations) do
    violationCount = violationCount + 1
end
for _ in pairs(exceptions) do
    exceptionCount = exceptionCount + 1
end

print(string.format("[conformance] surface: %d roots, %d members", rootCount, memberCount))
print(string.format("[conformance] violations: %d total, %d baselined", violationCount, exceptionCount))

local failures = #hardFailures + #newViolations + #staleExceptions

for _, msg in ipairs(hardFailures) do
    print("[conformance] FAIL " .. msg)
end
for _, id in ipairs(newViolations) do
    print("[conformance] FAIL new violation not in baseline: " .. id)
end
for _, id in ipairs(staleExceptions) do
    print("[conformance] FAIL stale baseline entry (fixed? remove it): " .. id)
end

if failures == 0 then
    print("[conformance] PASS")
else
    print(string.format("[conformance] FAILED with %d problem(s)", failures))
end

return failures
