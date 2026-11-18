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
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>

namespace vultra
{
    namespace
    {
        std::expected<void, Error> createParentDirectory(const std::filesystem::path& path)
        {
            std::error_code error;
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path(), error);
            }
            if (error)
            {
                return makeError("Create directory {}: {}", path.parent_path().string(), error.message());
            }
            return {};
        }
    } // namespace

    std::expected<Image, Error> loadPng(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            return makeError("Cannot open PNG: {}", path.string());
        }
        const auto length = input.tellg();
        if (length <= 0 || length > INT_MAX)
        {
            return makeError("PNG file size is unsupported");
        }
        std::vector<unsigned char> bytes(static_cast<size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!input)
        {
            return makeError("Cannot read PNG");
        }
        return loadPng(std::as_bytes(std::span(bytes)));
    }

    std::expected<Image, Error> loadPng(std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > INT_MAX)
        {
            return makeError("PNG data size is unsupported");
        }
        int                                                  width;
        int                                                  height;
        int                                                  channels;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                  int(bytes.size()),
                                  &width,
                                  &height,
                                  &channels,
                                  4),
            stbi_image_free);
        if (!pixels)
        {
            return makeError("PNG decode failed: {}", stbi_failure_reason());
        }
        Image image {{uint32_t(width), uint32_t(height)}, {}};
        image.rgba.resize(size_t(width) * height * 4);
        std::transform(pixels.get(),
                       pixels.get() + image.rgba.size(),
                       image.rgba.begin(),
                       [](unsigned char v)
                       {
                           return float(v) / 255;
                       });
        return image;
    }

    std::expected<void, Error> savePng(const Image& image, const std::filesystem::path& path)
    {
        if (auto result = validateImage(image); !result)
        {
            return result;
        }
        if (auto result = createParentDirectory(path); !result)
        {
            return result;
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
            return makeError("Cannot create PNG: {}", path.string());
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
            return makeError("PNG write failed: {}", path.string());
        }
        return {};
    }

    std::expected<void, Error> savePfm(const Image& image, const std::filesystem::path& path)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        if (auto result = validateImage(image); !result)
        {
            return result;
        }
        if (auto result = createParentDirectory(path); !result)
        {
            return result;
        }
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            return makeError("Cannot create PFM: {}", path.string());
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
            return makeError("PFM write failed: {}", path.string());
        }
        return {};
    }

    std::expected<Image, Error> loadPfm(const std::filesystem::path& path)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return makeError("Cannot open PFM: {}", path.string());
        }
        std::string kind;
        uint64_t    width  = 0;
        uint64_t    height = 0;
        double      scale  = 0;
        input >> kind >> width >> height >> scale;
        // One whitespace character ends the header.
        input.get();
        const uint32_t channels = kind == "PF" ? 3 : (kind == "Pf" ? 1 : 0);
        if (!input || channels == 0 || width == 0 || height == 0 || width > 65536 || height > 65536 || scale == 0)
        {
            return makeError("Not a PFM image: {}", path.string());
        }
        // A negative scale marks little-endian floats.
        const bool         swap = (scale < 0) != (std::endian::native == std::endian::little);
        std::vector<float> values(width * height * channels);
        input.read(reinterpret_cast<char*>(values.data()), std::streamsize(values.size() * sizeof(float)));
        if (!input)
        {
            return makeError("PFM data is shorter than its size: {}", path.string());
        }
        Image image {{uint32_t(width), uint32_t(height)}, std::vector<float>(width * height * 4, 1.0f)};
        // PFM stores rows from bottom to top; Image stores them from top to bottom.
        for (uint64_t y = 0; y < height; ++y)
        {
            for (uint64_t x = 0; x < width; ++x)
            {
                const auto source = ((height - 1 - y) * width + x) * channels;
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    float value = values[source + (channels == 3 ? channel : 0)];
                    if (swap)
                    {
                        value = std::bit_cast<float>(std::byteswap(std::bit_cast<uint32_t>(value)));
                    }
                    image.rgba[(y * width + x) * 4 + channel] = value;
                }
            }
        }
        return image;
    }

    std::expected<void, Error> dumpFrame(const Image& image, const std::filesystem::path& directory, uint64_t frame)
    {
        std::ostringstream name;
        name << "frame_" << std::setfill('0') << std::setw(6) << frame << ".png";
        const auto      path = directory / name.str();
        std::error_code error;
        if (std::filesystem::exists(path, error) || error)
        {
            return makeError("Frame already exists: {}", path.string());
        }
        return savePng(image, path);
    }
} // namespace vultra
