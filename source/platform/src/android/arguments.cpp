#include <vultra/platform/android/arguments.hpp>

#include <game-activity/GameActivity.h>

#include <stdexcept>

namespace vultra::platform
{
    namespace
    {
        class JavaScope
        {
        public:
            explicit JavaScope(JavaVM& vm) :
                m_Vm(vm)
            {
                const auto status = vm.GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
                if (status == JNI_EDETACHED)
                {
                    if (vm.AttachCurrentThread(&env, nullptr) != JNI_OK)
                    {
                        throw std::runtime_error("Attach GameActivity native thread to Java VM");
                    }
                    m_Attached = true;
                }
                else if (status != JNI_OK)
                {
                    throw std::runtime_error("Get GameActivity Java environment");
                }
                if (env->PushLocalFrame(16) != JNI_OK)
                {
                    if (m_Attached)
                    {
                        vm.DetachCurrentThread();
                    }
                    throw std::runtime_error("Allocate GameActivity JNI local frame");
                }
            }

            ~JavaScope()
            {
                env->PopLocalFrame(nullptr);
                if (m_Attached)
                {
                    m_Vm.DetachCurrentThread();
                }
            }

            void check(const char* operation)
            {
                if (env->ExceptionCheck())
                {
                    env->ExceptionClear();
                    throw std::runtime_error(operation);
                }
            }

            JNIEnv* env = nullptr;

        private:
            JavaVM& m_Vm;
            bool    m_Attached = false;
        };
    } // namespace

    std::vector<std::string> androidArguments(GameActivity& activity)
    {
        JavaScope  scope(*activity.vm);
        auto&      env           = *scope.env;
        const auto activityClass = env.GetObjectClass(activity.javaGameActivity);
        const auto getIntent     = env.GetMethodID(activityClass, "getIntent", "()Landroid/content/Intent;");
        scope.check("Find GameActivity Intent accessor");
        const auto intent = env.CallObjectMethod(activity.javaGameActivity, getIntent);
        scope.check("Read GameActivity Intent");
        const auto intentClass = env.GetObjectClass(intent);
        const auto getArguments =
            env.GetMethodID(intentClass, "getStringArrayExtra", "(Ljava/lang/String;)[Ljava/lang/String;");
        scope.check("Find Intent argument accessor");
        const auto key       = env.NewStringUTF("vultra.arguments");
        const auto arguments = static_cast<jobjectArray>(env.CallObjectMethod(intent, getArguments, key));
        scope.check("Read vultra.arguments from Intent");
        std::vector<std::string> result;
        if (!arguments)
        {
            return result;
        }
        const auto stringClass = env.FindClass("java/lang/String");
        const auto getBytes    = env.GetMethodID(stringClass, "getBytes", "(Ljava/lang/String;)[B");
        const auto encoding    = env.NewStringUTF("UTF-8");
        scope.check("Find UTF-8 string encoder");
        const auto count = env.GetArrayLength(arguments);
        for (jsize index = 0; index < count; ++index)
        {
            const auto argument = env.GetObjectArrayElement(arguments, index);
            if (!argument)
            {
                throw std::invalid_argument("GameActivity argument array contains null");
            }
            const auto bytes = static_cast<jbyteArray>(env.CallObjectMethod(argument, getBytes, encoding));
            scope.check("Encode GameActivity argument as UTF-8");
            const auto  size = env.GetArrayLength(bytes);
            std::string text(size_t(size), char(0));
            env.GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(text.data()));
            scope.check("Copy GameActivity argument");
            result.push_back(std::move(text));
            env.DeleteLocalRef(bytes);
            env.DeleteLocalRef(argument);
        }
        return result;
    }
} // namespace vultra::platform
