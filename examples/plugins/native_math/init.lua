-- Lua glue for the native_math plugin. The native library already registered native_math.length3()
-- by the time this runs, so the Lua side can build a friendlier API on top of it.
local M = {}

function M.on_install()
    if native_math and native_math.length3 then
        print(string.format("[native_math] length3(3,4,12) = %.1f", native_math.length3(3, 4, 12)))
    else
        print("[native_math] native binding missing")
    end
end

return M
