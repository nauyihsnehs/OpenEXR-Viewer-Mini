#pragma once

#include <model/ExrInput.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTiledInputPart.h>
#include <OpenEXR/ImfTileDescription.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

// Shared by decoding, document menus and scanline export of a selected level.
namespace MipLevels
{
    struct Geometry {
        Imath::Box2i data;
        Imath::Box2i display;
        int count = 1;
        bool tiled = false;
    };

    inline Imath::Box2i displayWindow(const Imf::Header& header, int level)
    {
        auto window = header.displayWindow();
        const bool roundUp = header.hasTileDescription()
          && header.tileDescription().roundingMode == Imf::ROUND_UP;
        for (int axis = 0; axis < 2; ++axis) {
            int64_t size = int64_t(window.max[axis]) - window.min[axis] + 1;
            if (size <= 0) throw std::runtime_error("Invalid display window.");
            for (int i = 0; i < level; ++i)
                size = std::max<int64_t>(1, (size + (roundUp ? 1 : 0)) / 2);
            if (size > std::numeric_limits<int>::max())
                throw std::runtime_error("Display window is too large.");
            window.max[axis] = int(int64_t(window.min[axis]) + size - 1);
        }
        return window;
    }

    inline Geometry query(const std::shared_ptr<ExrInput>& source, int part, int level = 0)
    {
        if (!source || !source->file || part < 0 || part >= source->file->parts())
            throw std::runtime_error("Invalid image part.");
        const std::lock_guard<std::mutex> lock(source->mutex);
        const auto& header = source->file->header(part);
        if (header.hasType() && header.type() != Imf::SCANLINEIMAGE && header.type() != Imf::TILEDIMAGE)
            throw std::runtime_error("Deep image parts are not supported.");
        Geometry result;
        result.tiled = header.hasType() ? header.type() == Imf::TILEDIMAGE : header.hasTileDescription();
        result.data = header.dataWindow();
        if (result.tiled) {
            if (header.tileDescription().mode == Imf::RIPMAP_LEVELS)
                throw std::runtime_error("Ripmap tiled images are not supported.");
            Imf::TiledInputPart input(*source->file, part);
            result.count = input.numLevels();
            if (level < 0 || level >= result.count)
                throw std::runtime_error("The selected mip level is not available.");
            result.data = input.dataWindowForLevel(level, level);
        } else if (level != 0) {
            throw std::runtime_error("Scanline image parts only have mip level 0.");
        }
        // Display-window scaling is an application convention; EXR stores only one.
        result.display = displayWindow(header, level);
        return result;
    }

    inline int commonCount(const std::shared_ptr<ExrInput>& source)
    {
        int count = std::numeric_limits<int>::max();
        for (int part = 0; part < source->file->parts(); ++part)
            count = std::min(count, query(source, part).count);
        return count == std::numeric_limits<int>::max() ? 1 : count;
    }
}
