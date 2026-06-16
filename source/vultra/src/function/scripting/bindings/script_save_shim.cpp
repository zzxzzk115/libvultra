#include "vultra/function/scripting/bindings/script_save_shim.hpp"

#include "vultra/function/services/save_service.hpp"

#include <stdexcept>
#include <string>

namespace vultra
{
    void saveSet(ScriptContext& ctx, const std::string& key, sol::object value)
    {
        if (!ctx.saveService)
            return;
        if (value.is<bool>())
            ctx.saveService->setBool(key, value.as<bool>());
        else if (value.is<double>())
            ctx.saveService->setNumber(key, value.as<double>());
        else if (value.is<std::string>())
            ctx.saveService->setString(key, value.as<std::string>());
        else
            throw std::runtime_error("Save.set expects a number, boolean, or string value");
    }

    sol::object saveGet(ScriptContext& ctx, sol::this_state luaState, const std::string& key, sol::object fallback)
    {
        sol::state_view lua(luaState);
        if (!ctx.saveService)
            return fallback;
        switch (ctx.saveService->valueType(key))
        {
            case ISaveService::ValueType::eNumber:
                return sol::make_object(lua, ctx.saveService->getNumber(key, 0.0));
            case ISaveService::ValueType::eBool:
                return sol::make_object(lua, ctx.saveService->getBool(key, false));
            case ISaveService::ValueType::eString:
                return sol::make_object(lua, ctx.saveService->getString(key, std::string {}));
            case ISaveService::ValueType::eNone:
            default:
                return fallback;
        }
    }

    bool saveHas(ScriptContext& ctx, const std::string& key)
    {
        return ctx.saveService ? ctx.saveService->has(key) : false;
    }
    void saveRemove(ScriptContext& ctx, const std::string& key)
    {
        if (ctx.saveService)
            ctx.saveService->remove(key);
    }
    void saveClear(ScriptContext& ctx)
    {
        if (ctx.saveService)
            ctx.saveService->clear();
    }

    bool saveSave(ScriptContext& ctx, const std::string& slot)
    {
        return ctx.saveService ? ctx.saveService->save(slot) : false;
    }
    bool saveLoad(ScriptContext& ctx, const std::string& slot)
    {
        return ctx.saveService ? ctx.saveService->load(slot) : false;
    }
    bool saveHasSlot(ScriptContext& ctx, const std::string& slot)
    {
        return ctx.saveService ? ctx.saveService->hasSlot(slot) : false;
    }
    bool saveDeleteSlot(ScriptContext& ctx, const std::string& slot)
    {
        return ctx.saveService ? ctx.saveService->deleteSlot(slot) : false;
    }
    sol::table saveListSlots(ScriptContext& ctx, sol::this_state luaState)
    {
        sol::state_view lua(luaState);
        sol::table      result = lua.create_table();
        if (!ctx.saveService)
            return result;
        int index = 1;
        for (const auto& slot : ctx.saveService->listSlots())
            result[index++] = slot;
        return result;
    }
} // namespace vultra
