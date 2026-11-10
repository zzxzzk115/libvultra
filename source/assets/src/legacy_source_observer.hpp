#pragma once

#include <vultra/assets/source_file.hpp>

#include <exception>

namespace vultra::asset_detail
{
    // Legacy callbacks may throw. Importers serialize observations and join their workers before this object expires.
    class LegacySourceObservation
    {
    public:
        explicit LegacySourceObservation(const SourceObserver& observer) :
            m_Observer(observer)
        {
        }

        ExpectedSourceObserver callback()
        {
            if (!m_Observer)
            {
                return {};
            }
            return [this](const auto& path, auto bytes) -> std::expected<void, std::string>
            {
                if (!m_Error)
                {
                    try
                    {
                        m_Observer(path, bytes);
                        return {};
                    }
                    catch (...)
                    {
                        m_Error = std::current_exception();
                    }
                }
                return std::unexpected("Legacy source observer failed");
            };
        }

        void rethrowError() const
        {
            if (m_Error)
            {
                std::rethrow_exception(m_Error);
            }
        }

    private:
        const SourceObserver& m_Observer;
        std::exception_ptr    m_Error;
    };
} // namespace vultra::asset_detail
