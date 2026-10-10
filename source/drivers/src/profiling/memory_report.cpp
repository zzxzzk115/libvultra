#include <vultra/drivers/profiling/memory_report.hpp>

#include <algorithm>

namespace vultra
{
    MemoryReport memoryReport(const Device& device)
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
            check(budgetResult, "Query VRI video memory budget");
        }
        uint32_t   count  = 0;
        const auto result = device.core.EnumerateObjects(device.handle, &count, nullptr);
        if (result == VriResult_Unsupported)
        {
            return report;
        }
        check(result, "Count VRI objects");
        report.objects.resize(count);
        check(device.core.EnumerateObjects(device.handle, &count, report.objects.data()), "Enumerate VRI objects");
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
