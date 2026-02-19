#pragma once

#include <string>
#include <vector>

namespace vultra::openxr
{
    /**
     * Represents a single binding:
     *   action name  ->  OpenXR component path
     */
    struct XRInputBinding
    {
        std::string actionName;
        std::string path;
    };

    /**
     * Describes an OpenXR interaction profile and its action bindings.
     *
     * Example profile path:
     *   "/interaction_profiles/valve/index_controller"
     *
     * The engine does not assume any specific device capability;
     * profiles are provided by the application layer.
     */
    class XRInputProfile
    {
    public:
        explicit XRInputProfile(const std::string& profilePath);

        // Add a binding to this profile
        XRInputProfile& add(const std::string& actionName, const std::string& componentPath);

        const std::string& getProfilePath() const { return m_ProfilePath; }

        const std::vector<XRInputBinding>& getBindings() const { return m_Bindings; }

    private:
        std::string                 m_ProfilePath;
        std::vector<XRInputBinding> m_Bindings;
    };

} // namespace vultra::openxr