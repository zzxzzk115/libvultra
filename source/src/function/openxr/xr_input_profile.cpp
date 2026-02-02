#include "vultra/function/openxr/xr_input_profile.hpp"

namespace vultra::openxr
{
    XRInputProfile::XRInputProfile(const std::string& profilePath) : m_ProfilePath(profilePath) {}

    XRInputProfile& XRInputProfile::add(const std::string& actionName, const std::string& componentPath)
    {
        m_Bindings.push_back({actionName, componentPath});
        return *this;
    }

} // namespace vultra::openxr