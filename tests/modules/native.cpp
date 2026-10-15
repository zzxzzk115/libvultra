#include <vultra/scripting/native_script.hpp>

namespace
{
#ifdef TEST_REPLACEMENT
    constexpr float kFirstPosition = 3;
#else
    constexpr float kFirstPosition = 1;
#endif

    class First final : public vultra::scripting::NativeScript
    {
    public:
        void onProcess(float) override
        {
            actor().setPosition({kFirstPosition, 0, 0});
        }

        void onGui(const vultra::scripting::EditorGui& gui) override
        {
            gui.text("First native script");
        }
    };

    class Second final : public vultra::scripting::NativeScript
    {
    public:
        void onProcess(float) override
        {
            actor().setPosition({kFirstPosition + 1, 0, 0});
        }

        void onGui(const vultra::scripting::EditorGui& gui) override
        {
            gui.text("Second native script");
        }
    };
} // namespace

VULTRA_NATIVE_MODULE(VULTRA_NATIVE_CLASS(First), VULTRA_NATIVE_CLASS(Second))
