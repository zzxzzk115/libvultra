#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct PassReflection
{
    struct IO
    {
        std::string name;
        std::string type; // "Texture", "Buffer" ...
    };
    std::vector<IO> inputs;
    std::vector<IO> outputs;
};

class RenderPass
{
public:
    virtual ~RenderPass()                  = default;
    virtual const char*    getName() const = 0;
    virtual PassReflection reflect() const = 0;
};

// ----------------------------------------
// PassRegistry
// ----------------------------------------
struct PassRegistryEntry
{
    std::string                                  name;
    std::function<std::shared_ptr<RenderPass>()> factory;
};

class PassRegistry
{
public:
    static PassRegistry& instance()
    {
        static PassRegistry inst;
        return inst;
    }

    void registerPass(const std::string& name, std::function<std::shared_ptr<RenderPass>()> factory)
    {
        m_Entries[name] = {name, factory};
    }

    const std::unordered_map<std::string, PassRegistryEntry>& getEntries() const { return m_Entries; }

private:
    std::unordered_map<std::string, PassRegistryEntry> m_Entries;
};

#define REGISTER_PASS(PASS_TYPE) \
    static bool s_##PASS_TYPE##_registered = []() { \
        PassRegistry::instance().registerPass(#PASS_TYPE, []() { return std::make_shared<PASS_TYPE>(); }); \
        return true; \
    }()
