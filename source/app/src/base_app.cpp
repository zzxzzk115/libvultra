#include <vultra/app/base_app.hpp>

namespace vultra
{
    std::expected<void, Error> BaseApp::onPreUpdate(float)
    {
        return {};
    }

    std::expected<void, Error> BaseApp::onUpdate(float)
    {
        return {};
    }

    std::expected<void, Error> BaseApp::onPostUpdate(float)
    {
        return {};
    }
} // namespace vultra
