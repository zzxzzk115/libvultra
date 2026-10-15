#include <vultra/core/image/image.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <bit>
#include <climits>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vultra
{
    Image loadPng(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error("Cannot open PNG: " + path.string());
        }
        const auto length = input.tellg();
        if (length <= 0 || length > INT_MAX)
        {
            throw std::runtime_error("PNG file size is unsupported");
        }
        std::vector<unsigned char> bytes(static_cast<size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!input)
        {
            throw std::runtime_error("Cannot read PNG");
        }
        return loadPng(std::as_bytes(std::span(bytes)));
    }

    Image loadPng(std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > INT_MAX)
        {
            throw std::invalid_argument("PNG data size is unsupported");
        }
        int   width;
        int   height;
        int   channels;
        auto* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                             int(bytes.size()),
                                             &width,
                                             &height,
                                             &channels,
                                             4);
        if (!pixels)
        {
            throw std::runtime_error(std::string("PNG decode failed: ") + stbi_failure_reason());
        }
        Image image {{uint32_t(width), uint32_t(height)}, {}};
        try
        {
            image.rgba.resize(size_t(width) * height * 4);
            std::transform(pixels,
                           pixels + image.rgba.size(),
                           image.rgba.begin(),
                           [](unsigned char v)
                           {
                               return float(v) / 255;
                           });
        }
        catch (...)
        {
            stbi_image_free(pixels);
            throw;
        }
        stbi_image_free(pixels);
        return image;
    }

    void savePng(const Image& image, const std::filesystem::path& path)
    {
        validateImage(image);
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::vector<unsigned char> bytes(image.rgba.size());
        std::transform(image.rgba.begin(),
                       image.rgba.end(),
                       bytes.begin(),
                       [](float v)
                       {
                           return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f);
                       });
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Cannot create PNG: " + path.string());
        }
        const auto write = [](void* context, void* data, int size)
        {
            static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
        };
        const auto success = stbi_write_png_to_func(write,
                                                    &output,
                                                    int(image.size.width),
                                                    int(image.size.height),
                                                    4,
                                                    bytes.data(),
                                                    int(image.size.width * 4));
        output.flush();
        if (!success || !output)
        {
            throw std::runtime_error("PNG write failed: " + path.string());
        }
    }

    void savePfm(const Image& image, const std::filesystem::path& path)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        validateImage(image);
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Cannot create PFM: " + path.string());
        }
        output << "PF\n"
               << image.size.width << ' ' << image.size.height << '\n'
               << (std::endian::native == std::endian::little ? "-1.0\n" : "1.0\n");
        std::vector<float> row(size_t(image.size.width) * 3);
        // PFM stores RGB from bottom to top; Image stores RGBA from top to bottom.
        for (uint32_t y = image.size.height; y > 0; --y)
        {
            for (uint32_t x = 0; x < image.size.width; ++x)
            {
                const auto source = (size_t(y - 1) * image.size.width + x) * 4;
                std::copy_n(image.rgba.data() + source, 3, row.data() + size_t(x) * 3);
            }
            output.write(reinterpret_cast<const char*>(row.data()), std::streamsize(row.size() * sizeof(float)));
        }
        output.flush();
        if (!output)
        {
            throw std::runtime_error("PFM write failed: " + path.string());
        }
    }

    void dumpFrame(const Image& image, const std::filesystem::path& directory, uint64_t frame)
    {
        std::ostringstream name;
        name << "frame_" << std::setfill('0') << std::setw(6) << frame << ".png";
        const auto path = directory / name.str();
        if (std::filesystem::exists(path))
        {
            throw std::runtime_error("Frame already exists: " + path.string());
        }
        savePng(image, path);
    }
} // namespace vultra
