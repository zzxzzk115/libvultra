#include <vultra/drivers/profiling/benchmark.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        std::string jsonString(const std::string& value)
        {
            std::string result = "\"";
            for (const unsigned char ch : value)
            {
                if (ch == '"' || ch == '\\')
                {
                    result += '\\';
                    result += char(ch);
                }
                else if (ch < 0x20)
                {
                    constexpr char hex[] = "0123456789abcdef";
                    result += "\\u00";
                    result += hex[ch >> 4];
                    result += hex[ch & 15];
                }
                else
                {
                    result += char(ch);
                }
            }
            return result + '"';
        }

        std::string csvString(const std::string& value)
        {
            std::string result = "\"";
            for (const char ch : value)
            {
                if (ch == '"')
                {
                    result += '"';
                }
                result += ch;
            }
            return result + '"';
        }

        void writeSummary(std::ostream& out, const std::string& name, std::vector<double> values)
        {
            if (values.empty())
            {
                return;
            }
            std::sort(values.begin(), values.end());
            const auto   count  = values.size();
            const double mean   = std::accumulate(values.begin(), values.end(), 0.0) / double(count);
            const double median = count % 2 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2;
            const double p95    = values[size_t(std::ceil(0.95 * double(count))) - 1];
            out << csvString(name) << ',' << count << ',' << mean << ',' << median << ',' << p95 << ','
                << values.front() << ',' << values.back() << '\n';
        }
    } // namespace

    void BenchmarkCapture::add(const FrameTiming& frame, std::span<const PassTiming> passes)
    {
        if (!m_Samples.empty() && frame.frameIndex != m_Samples.back().frame.frameIndex + 1)
        {
            throw std::invalid_argument("Benchmark frames must be consecutive");
        }
        m_Samples.push_back({frame, {passes.begin(), passes.end()}});
    }

    void BenchmarkCapture::write(const std::filesystem::path& directory,
                                 const BenchmarkMetadata&     metadata,
                                 const VriDeviceDesc&         device) const
    {
        if (m_Samples.empty())
        {
            throw std::invalid_argument("Benchmark has no measured frames");
        }
        if (std::filesystem::exists(directory))
        {
            throw std::invalid_argument("Benchmark output directory already exists");
        }
        std::filesystem::create_directories(directory);
        std::ofstream manifest(directory / "manifest.json");
        std::ofstream frames(directory / "frames.csv");
        std::ofstream passes(directory / "passes.csv");
        std::ofstream summary(directory / "summary.csv");
        if (!manifest || !frames || !passes || !summary)
        {
            throw std::runtime_error("Cannot create benchmark output files");
        }
        manifest << "{\n  \"experiment\": " << jsonString(metadata.experiment)
                 << ",\n  \"source_revision\": " << jsonString(metadata.sourceRevision)
                 << ",\n  \"shader_hash_fnv1a64\": " << jsonString(metadata.shaderHash)
                 << ",\n  \"window_system\": " << jsonString(metadata.windowSystem)
                 << ",\n  \"build_mode\": " << jsonString(metadata.buildMode)
                 << ",\n  \"validation\": " << (metadata.validation ? "true" : "false")
                 << ",\n  \"width\": " << metadata.width << ",\n  \"height\": " << metadata.height
                 << ",\n  \"warmup_frames\": " << metadata.warmupFrames
                 << ",\n  \"measured_frames\": " << m_Samples.size() << ",\n  \"present_mode\": \"fifo\""
                 << ",\n  \"adapter\": " << jsonString(device.adapter.name)
                 << ",\n  \"vendor_id\": " << device.adapter.vendorId
                 << ",\n  \"device_id\": " << device.adapter.deviceId
                 << ",\n  \"graphics_api\": " << int(device.graphicsAPI) << ",\n  \"api_version\": "
                 << jsonString(std::to_string(device.apiVersionMajor) + "." + std::to_string(device.apiVersionMinor))
                 << ",\n  \"gpu_timestamps\": " << (device.hasTimestampQueries ? "true" : "false")
                 << ",\n  \"parameters\": {\n";
        for (size_t i = 0; i < metadata.parameters.size(); ++i)
        {
            const auto& [name, value] = metadata.parameters[i];
            manifest << "    " << jsonString(name) << ": " << jsonString(value)
                     << (i + 1 == metadata.parameters.size() ? "\n" : ",\n");
        }
        manifest << "  }\n}\n";

        frames << "frame,total_ms,update_ms,acquire_ms,prepare_record_ms,submit_wait_ms,post_render_ms,present_ms,gpu_"
                  "ms\n";
        passes
            << "frame,pass,cpu_total_ms,cpu_barrier_ms,cpu_commands_ms,gpu_total_ms,gpu_barrier_ms,gpu_commands_ms\n";
        summary << "metric,count,mean_ms,median_ms,p95_ms,min_ms,max_ms\n";
        frames << std::setprecision(10);
        passes << std::setprecision(10);
        summary << std::setprecision(10);
        std::map<std::string, std::vector<double>> metrics;
        for (const auto& sample : m_Samples)
        {
            const auto& f = sample.frame;
            frames << f.frameIndex << ',' << f.totalMs << ',' << f.updateMs << ',' << f.acquireMs << ','
                   << f.prepareRecordMs << ',' << f.submitWaitMs << ',' << f.postRenderMs << ',' << f.presentMs << ',';
            if (f.gpuMs)
            {
                frames << *f.gpuMs;
                metrics["frame.gpu"].push_back(*f.gpuMs);
            }
            frames << '\n';
            metrics["frame.total"].push_back(f.totalMs);
            metrics["frame.update"].push_back(f.updateMs);
            metrics["frame.acquire"].push_back(f.acquireMs);
            metrics["frame.prepare_record"].push_back(f.prepareRecordMs);
            metrics["frame.submit_wait"].push_back(f.submitWaitMs);
            metrics["frame.post_render"].push_back(f.postRenderMs);
            metrics["frame.present"].push_back(f.presentMs);
            for (const auto& pass : sample.passes)
            {
                passes << f.frameIndex << ',' << csvString(pass.name) << ',' << pass.cpuMs << ',' << pass.cpuBarrierMs
                       << ',' << pass.cpuMs - pass.cpuBarrierMs << ',';
                metrics["pass." + pass.name + ".cpu_total"].push_back(pass.cpuMs);
                metrics["pass." + pass.name + ".cpu_barrier"].push_back(pass.cpuBarrierMs);
                if (f.gpuMs)
                {
                    passes << pass.gpuMs << ',' << pass.gpuBarrierMs << ',' << pass.gpuMs - pass.gpuBarrierMs;
                    metrics["pass." + pass.name + ".gpu_total"].push_back(pass.gpuMs);
                    metrics["pass." + pass.name + ".gpu_barrier"].push_back(pass.gpuBarrierMs);
                }
                else
                {
                    passes << ",,";
                }
                passes << '\n';
            }
        }
        for (const auto& [name, values] : metrics)
        {
            writeSummary(summary, name, values);
        }
        if (!manifest || !frames || !passes || !summary)
        {
            throw std::runtime_error("Writing benchmark output failed");
        }
    }
} // namespace vultra
