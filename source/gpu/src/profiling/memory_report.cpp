#include <vultra/gpu/profiling/memory_report.hpp>

#include <algorithm>

namespace vultra
{
    std::expected<MemoryReport, Error> memoryReport(const Device& device)
    {
        MemoryReport       report;
        VriVideoMemoryInfo video {};
        const auto budgetResult = device.core.GetVideoMemoryInfo(device.handle, VriMemoryLocation_Device, &video);
        if (budgetResult == VriResult_Success)
        {
            report.video = video;
        }
        else if (budgetResult != VriResult_Unsupported)
        {
            return std::unexpected(check(budgetResult, "Query VRI video memory budget").error());
        }
        uint32_t   count  = 0;
        const auto result = device.core.EnumerateObjects(device.handle, &count, nullptr);
        if (result == VriResult_Unsupported)
        {
            return report;
        }
        if (auto counted = check(result, "Count VRI objects"); !counted)
        {
            return std::unexpected(counted.error());
        }
        report.objects.resize(count);
        if (auto listed = check(device.core.EnumerateObjects(device.handle, &count, report.objects.data()),
                                "Enumerate VRI objects");
            !listed)
        {
            return std::unexpected(listed.error());
        }
        report.objects.resize(count);
        report.trackedBytes = 0;
        for (const auto& object : report.objects)
        {
            *report.trackedBytes += object.memoryBytes;
        }
        std::ranges::sort(report.objects, std::greater<>(), &VriObjectInfo::memoryBytes);
        return report;
    }
} // namespace vultra
