//
// Created by loulfy on 16/12/2023.
//

#include "loader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <set>

namespace ler::img
{
    static const std::set<fs::path> c_supportedImages =
    {
        ".png",
        ".jpg",
        ".jpeg",
        ".dds",
        ".ktx",
        ".tga"
    };

    StbImage::StbImage(const std::vector<char>& blob)
    {
        auto buff = reinterpret_cast<const stbi_uc*>(blob.data());
        image = stbi_load_from_memory(buff, static_cast<int>(blob.size()), &width, &height, &level, STBI_rgb_alpha);
    }

    StbImage::StbImage(const fs::path& path)
    {
        image = stbi_load(path.string().c_str(), &width, &height, &level, STBI_rgb_alpha);
    }

    StbImage::~StbImage()
    {
        stbi_image_free(image);
    }

    GliImage::GliImage(const std::vector<char>& blob)
    {
        image = gli::load(blob.data(), blob.size());
        if(image.empty())
            log::error("GLI failed to load blob");
    }

    GliImage::GliImage(const fs::path& path)
    {
        image = gli::load(path.string().c_str());
    }

    rhi::Format GliImage::format() const
    {
        try
        {
            return convert_format(image.format());
        }
        catch(const std::exception& e)
        {
            log::error(std::string("GLI: ") + e.what());
        }
        return rhi::Format::UNKNOWN;
    }

    ImagePtr ImageLoader::load(const fs::path& path)
    {
        const auto ext = path.extension();
        if(support(ext))
        {
            if(ext == ".dds" || ext == ".ktx")
                return std::make_shared<GliImage>(path);
            else
                return std::make_shared<StbImage>(path);
        }

        log::error("Format not supported: " + ext.string());
        return {};
    }

    bool ImageLoader::support(const fs::path& ext)
    {
        return c_supportedImages.contains(ext);
    }
}