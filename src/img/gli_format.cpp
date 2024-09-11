//
// Created by loulfy on 16/12/2023.
//

#include "loader.hpp"

namespace ler::img
{
    struct GLIMapping
    {
        rhi::Format rhiFormat;
        gli::format gliFormat;
    };

    // GLI
    rhi::Format gli_convert_format(gli::format format) {
        switch (format) {
            default:
                throw std::runtime_error("Unknown format");
        }
    }

    rhi::Format GliImage::convert_format(gli::format format)
    {
        return gli_convert_format(format);
    }
}