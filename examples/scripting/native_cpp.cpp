#include <vultra/scripting/native_script.hpp>

#include <cmath>
#include <cstdio>
#include <optional>

namespace
{
    constexpr float kOrbitRadius = 0.68f;
    constexpr float kTau         = 6.2831853f;

    class ShipOrbit final : public vultra::scripting::NativeScript
    {
    public:
        void onStart() override
        {
            m_Throttle.emplace(sceneRoot().child(2));
        }

        void onUpdate(float deltaSeconds) override
        {
            const auto throttle = m_Throttle->position();
            m_Speed             = 1.5f + throttle.x;
            m_Angle             = std::fmod(m_Angle + deltaSeconds * m_Speed, kTau);
            actor().setPosition({kOrbitRadius * std::cos(m_Angle), kOrbitRadius * std::sin(m_Angle), 0.0f});
        }

        void onGui(const vultra::scripting::EditorGui& gui) override
        {
            char      message[112];
            const int length =
                std::snprintf(message, sizeof(message), "Native C++  |  Ship orbit: %.2f rad/s", m_Speed);
            if (length < 0 || length >= int(sizeof(message)))
            {
                throw std::runtime_error("Format ship speed");
            }
            gui.text({message, size_t(length)});
        }

    private:
        std::optional<vultra::scripting::SceneNode> m_Throttle;
        float                                       m_Angle = 0.0f;
        float                                       m_Speed = 0.0f;
    };
} // namespace

VULTRA_NATIVE_SCRIPT(ShipOrbit)
