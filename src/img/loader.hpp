//
// Created by loulfy on 16/12/2023.
//

#pragma once

#include "rhi/rhi.hpp"
#include "sys/utils.hpp"
#include <gli/gli.hpp>

namespace ler::img
{
    class IImage
    {
    public:

        virtual ~IImage() = default;
        [[nodiscard]] virtual rhi::Extent extent() const = 0;
        [[nodiscard]] virtual unsigned char* data() const = 0;
        [[nodiscard]] virtual rhi::Format format() const = 0;
        [[nodiscard]] virtual size_t byteSize() const = 0;
    };

    using ImagePtr = std::shared_ptr<IImage>;

    class StbImage : public IImage
    {
    private:

        int width = 0;
        int height = 0;
        int level = 0;
        unsigned char* image = nullptr;

    public:

        StbImage() = default;
        StbImage(const StbImage&) = delete;
        explicit StbImage(const std::vector<char>& blob);
        explicit StbImage(const fs::path& path);
        ~StbImage() override;

        [[nodiscard]] rhi::Extent extent() const override { return {uint32_t(width), uint32_t(height)}; }
        [[nodiscard]] unsigned char* data() const override { return image; }
        [[nodiscard]] rhi::Format format() const override { return rhi::Format::RGBA8_UNORM; }
        [[nodiscard]] size_t byteSize() const override { return width * height * 4; }
    };

    class GliImage : public IImage
    {
    private:

        gli::texture image;

    public:

        explicit GliImage(const std::vector<char>& blob);
        explicit GliImage(const fs::path& path);

        [[nodiscard]] rhi::Extent extent() const override { return {uint32_t(image.extent().x), uint32_t(image.extent().y)}; }
        [[nodiscard]] unsigned char* data() const override { return (unsigned char *) image.data(); }
        [[nodiscard]] size_t byteSize() const override { return image.size(); }
        [[nodiscard]] rhi::Format format() const override;

        static rhi::Format convert_format(gli::format format);
    };

    struct ImageLoader
    {
        static ImagePtr load(const fs::path& path);
        static bool support(const fs::path& path);
    };
}