#include <vultra/scripting/native_script.hpp>

namespace
{
    class First final : public vultra::scripting::NativeScript
    {
    public:
        void onUpdate(float) override
        {
            actor().setPosition({1.0f, 0.0f, 0.0f});
        }
    };

    class Second final : public vultra::scripting::NativeScript
    {
    public:
        void onUpdate(float) override
        {
            actor().setPosition({2.0f, 0.0f, 0.0f});
        }
    };
} // namespace

VULTRA_NATIVE_MODULE(VULTRA_NATIVE_CLASS(First), VULTRA_NATIVE_CLASS(Second))
