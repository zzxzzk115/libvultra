#include <vultra/scripting/native_script.hpp>

#include <cmath>
#include <fstream>
#include <string>

namespace
{
    class LifecycleProbe final : public vultra::scripting::NativeScript
    {
    public:
        void onEnterTree() override
        {
            record("enter");
        }

        void onReady() override
        {
            auto value = actor().position();
            value.x += 1;
            actor().setPosition(value);
            actor().setProcessPriority(-7);
            setTimeScale(timeScale());
            if (!actor().isInsideTree() || actor().processPriority() != -7 || timeScale() != 1)
            {
                throw std::runtime_error("Native scene phase/priority ABI is inconsistent");
            }
            if (actor().propertyJson("name") != "\"" + std::string(actor().name()) + "\"")
            {
                throw std::runtime_error("Native inherited property differs from Node access");
            }
            actor().setPropertyJson("processPriority", "-7");
            record("ready");
        }

        void onPhysicsProcess(float delta) override
        {
            if (std::abs(delta - 1.0f / 60.0f) > 1e-7f || physicsFrame() == 0)
            {
                throw std::runtime_error("Native fixed update has no scene clock");
            }
            const auto input = action("jump");
            if (!input.held || input.pressed != (physicsFrame() == 1))
            {
                throw std::runtime_error("Native fixed input repeated or lost its edge");
            }
            auto value = actor().position();
            value.y += 1;
            actor().setPosition(value);
            record("fixed");
        }

        void onProcess(float) override
        {
            auto value = actor().position();
            value.z += 1;
            actor().setPosition(value);
            record("process");
        }

        void onExitTree() override
        {
            if (!actor().isInsideTree())
            {
                throw std::runtime_error("Native exit lost tree access");
            }
            record("exit");
        }

    private:
        void record(const char* event) const
        {
            const auto    file = "build/.tmp/" + std::string(sceneRoot().name()) + ".txt";
            std::ofstream output(file, std::ios::app);
            output << actor().name() << ':' << event << '\n';
            if (!output)
            {
                throw std::runtime_error("Write native lifecycle trace");
            }
        }
    };
} // namespace

VULTRA_NATIVE_SCRIPT(LifecycleProbe)
