#pragma once

#include "ResolutionLevel.h"
#include <model/ExrInput.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTiledInputPart.h>
#include <OpenEXR/ImfTileDescription.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <vector>

// Shared by decoding, document menus and scanline export of a selected level.
namespace ResolutionLevels
{
    struct Geometry {
        Imath::Box2i data;
        Imath::Box2i display;
        std::vector<ResolutionLevel> levels = {{0, 0}};
        bool tiled = false;
    };

    inline Imath::Box2i displayWindow(const Imf::Header& header, ResolutionLevel level)
    {
        if (level.x < 0 || level.y < 0)
            throw std::runtime_error("Invalid resolution level.");
        auto window = header.displayWindow();
        const bool roundUp = header.hasTileDescription()
          && header.tileDescription().roundingMode == Imf::ROUND_UP;
        for (int axis = 0; axis < 2; ++axis) {
            int64_t size = int64_t(window.max[axis]) - window.min[axis] + 1;
            if (size <= 0) throw std::runtime_error("Invalid display window.");
            const int exponent = axis == 0 ? level.x : level.y;
            for (int i = 0; i < exponent && size > 1; ++i)
                size = std::max<int64_t>(1, (size + (roundUp ? 1 : 0)) / 2);
            if (size > std::numeric_limits<int>::max())
                throw std::runtime_error("Display window is too large.");
            window.max[axis] = int(int64_t(window.min[axis]) + size - 1);
        }
        return window;
    }

    inline Geometry query(const std::shared_ptr<ExrInput>& source, int part, ResolutionLevel level = {})
    {
        if (!source || !source->file || part < 0 || part >= source->file->parts())
            throw std::runtime_error("Invalid image part.");
        const std::lock_guard<std::mutex> lock(source->mutex);
        const auto& header = source->file->header(part);
        if (header.hasType() && header.type() != Imf::SCANLINEIMAGE
            && header.type() != Imf::TILEDIMAGE && header.type() != Imf::DEEPSCANLINE)
            throw std::runtime_error("Deep Tiled image parts are not supported.");
        Geometry result;
        result.tiled = header.hasType() ? header.type() == Imf::TILEDIMAGE : header.hasTileDescription();
        result.data = header.dataWindow();
        if (result.tiled) {
            Imf::TiledInputPart input(*source->file, part);
            if (level.x < 0 || level.y < 0
                || level.x >= input.numXLevels() || level.y >= input.numYLevels()
                || !input.isValidLevel(level.x, level.y))
                throw std::runtime_error("The selected resolution level is not available.");
            result.levels.clear();
            for (int x = 0; x < input.numXLevels(); ++x)
                for (int y = 0; y < input.numYLevels(); ++y)
                    if (input.isValidLevel(x, y)) result.levels.emplace_back(x, y);
            result.data = input.dataWindowForLevel(level.x, level.y);
        } else if (level != ResolutionLevel()) {
            throw std::runtime_error("Scanline image parts only have resolution level (0, 0).");
        }
        // Display-window scaling is an application convention; EXR stores only one.
        result.display = displayWindow(header, level);
        return result;
    }

    // Inputs come from query() and are sorted by X, then Y. Intersect actual
    // pairs so a mixed Mipmap/Ripmap file cannot acquire nonexistent levels.
    inline std::vector<ResolutionLevel> intersection(
      const std::vector<ResolutionLevel>& left, const std::vector<ResolutionLevel>& right)
    {
        std::vector<ResolutionLevel> levels;
        std::set_intersection(left.begin(), left.end(), right.begin(), right.end(),
                              std::back_inserter(levels));
        return levels;
    }

    inline std::vector<ResolutionLevel> commonLevels(const std::shared_ptr<ExrInput>& source)
    {
        auto levels = query(source, 0).levels;
        for (int part = 1; part < source->file->parts(); ++part)
            levels = intersection(levels, query(source, part).levels);
        return levels;
    }
}
