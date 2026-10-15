#include <vultra/assets/material_instance.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    constexpr std::string_view document = R"({
        "version": 1,
        "shader": "01234567-89ab-cdef-0123-456789abcdef",
        "variant": "Default",
        "properties": {
            "gain": {"type": 0, "value": 2.5},
            "count": {"type": 1, "value": 7},
            "enabled": {"type": 2, "value": true},
            "color": {"type": 3, "value": [1, 0.5, 0.25, 1]},
            "texture": {"type": 4, "value": {
                "asset": null, "builtin": "white", "scale_offset": [1, 1, 0, 0]
            }}
        }
    })";
    auto                       instance = MaterialInstance::parse(document);
    require(instance.overrides().size() == 5 && std::get<float>(instance.overrides().at("gain")) == 2.5f &&
                std::get<int32_t>(instance.overrides().at("count")) == 7 &&
                std::get<bool>(instance.overrides().at("enabled")) &&
                std::get<std::array<float, 4>>(instance.overrides().at("color"))[1] == 0.5f &&
                std::get<ShaderTextureValue>(instance.overrides().at("texture")).builtin == "white",
            "CPU material loading lost typed overrides");
    const auto restored = MaterialInstance::parse(instance.serialize());
    require(restored.shader == instance.shader && restored.variant == instance.variant &&
                restored.overrides() == instance.overrides(),
            "CPU material round trip changed its data");
    const auto revision = instance.revision();
    instance.reset("gain");
    require(!instance.overrides().contains("gain") && instance.revision() == revision + 1,
            "CPU material reset did not publish its change");
    instance.reset("gain");
    require(instance.revision() == revision + 1, "Missing material override changed its revision");
    auto invalid = std::string(document);
    invalid.replace(invalid.find("\"version\": 1"), 12, "\"version\": 2");
    bool rejected = false;
    try
    {
        MaterialInstance::parse(invalid);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "CPU material accepted an unsupported format version");
    std::cout << "CPU material passed: typed persistence, round trip, revision and invalid version\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
