#pragma once

#include <OpenEXR/ImfHeader.h>
#include <string>
#include <vector>

// Preserve attribute presence as well as values, including an empty multiView.
struct ViewMetadata {
    bool hasView = false;
    bool hasMultiView = false;
    std::string view;
    std::vector<std::string> multiView;

    static ViewMetadata read(const Imf::Header& header);
    static bool stereoGeometryMatches(const Imf::Header& left, const Imf::Header& right, int level = 0);
    void write(Imf::Header& header) const;
    bool present() const { return hasView || hasMultiView; }
    std::string defaultView() const;
    std::string channelView(const std::string& channel) const;
};
