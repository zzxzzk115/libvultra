local status = Upscaler.status()
print(string.format(
    "[noop_upscaler.lua] active=%s available=%s enabled=%s mode=%s message=%s",
    tostring(status.active),
    tostring(status.available),
    tostring(status.enabled),
    tostring(status.mode),
    tostring(status.message)
))

return {
    on_install = function()
        local providers = Upscaler.providers()
        print(string.format("[noop_upscaler.lua] provider count=%d", #providers))
    end
}
