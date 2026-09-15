#include "PixelDiagnostics.h"

#include <algorithm>

namespace
{
    struct Run {
        int left;
        int right;
        size_t node;
    };

    struct Node {
        size_t parent;
        FramebufferData::AnomalyRegion region;
    };

    size_t root(std::vector<Node>& nodes, size_t index)
    {
        while (nodes[index].parent != index) {
            nodes[index].parent = nodes[nodes[index].parent].parent;
            index = nodes[index].parent;
        }
        return index;
    }

    void join(std::vector<Node>& nodes, size_t a, size_t b)
    {
        a = root(nodes, a);
        b = root(nodes, b);
        if (a == b) return;
        if (nodes[a].region.pixelCount < nodes[b].region.pixelCount)
            std::swap(a, b);
        nodes[b].parent = a;
        nodes[a].region.bounds = nodes[a].region.bounds.united(nodes[b].region.bounds);
        nodes[a].region.pixelCount += nodes[b].region.pixelCount;
        nodes[a].region.flags |= nodes[b].region.flags;
    }
}

std::vector<FramebufferData::AnomalyRegion> PixelDiagnostics::connectedRegions(
  const std::vector<uint8_t>& flags, int width, int height,
  const Cancellation& cancel)
{
    if (flags.empty()) return {};
    std::vector<Node> nodes;
    std::vector<Run> previous, current;
    for (int y = 0; y < height; ++y) {
        if (cancel->load()) return {};
        current.clear();
        size_t firstPrevious = 0;
        for (int x = 0; x < width;) {
            if ((x & 4095) == 0 && cancel->load()) return {};
            if (!flags[size_t(y) * width + x]) {
                ++x;
                continue;
            }
            const int left = x;
            uint8_t kinds = 0;
            while (x < width && flags[size_t(y) * width + x]) {
                if ((x & 4095) == 0 && cancel->load()) return {};
                kinds |= flags[size_t(y) * width + x++];
            }
            const int right = x - 1;
            const size_t index = nodes.size();
            FramebufferData::AnomalyRegion region;
            region.bounds = QRect(left, y, x - left, 1);
            region.pixelCount = uint64_t(x - left);
            region.flags = kinds;
            nodes.push_back({index, region});
            current.push_back({left, right, index});
            // Runs touching diagonally belong to the same eight-connected region.
            while (firstPrevious < previous.size()
                   && previous[firstPrevious].right < left - 1)
                ++firstPrevious;
            for (size_t p = firstPrevious;
                 p < previous.size() && previous[p].left <= right + 1; ++p) {
                if ((p & 4095) == 0 && cancel->load()) return {};
                join(nodes, index, previous[p].node);
            }
        }
        previous.swap(current);
    }
    std::vector<FramebufferData::AnomalyRegion> regions;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if ((i & 4095) == 0 && cancel->load()) return {};
        if (nodes[i].parent == i) regions.push_back(nodes[i].region);
    }
    return regions;
}
