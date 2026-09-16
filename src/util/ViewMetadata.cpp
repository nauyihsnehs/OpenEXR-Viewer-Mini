#include "ViewMetadata.h"
#include <OpenEXR/ImfMultiView.h>
#include <OpenEXR/ImfStringVectorAttribute.h>

bool ViewMetadata::stereoGeometryMatches(const Imf::Header& left, const Imf::Header& right)
{
    return left.displayWindow() == right.displayWindow()
           && left.pixelAspectRatio() == right.pixelAspectRatio();
}

ViewMetadata ViewMetadata::read(const Imf::Header& header)
{
    ViewMetadata result;
    result.hasView = header.hasView();
    if (result.hasView) result.view = header.view();
    const auto* attribute = header.findTypedAttribute<Imf::StringVectorAttribute>("multiView");
    result.hasMultiView = attribute != nullptr;
    if (attribute) result.multiView = attribute->value();
    return result;
}

void ViewMetadata::write(Imf::Header& header) const
{
    if (hasView) header.setView(view);
    if (hasMultiView) header.insert("multiView", Imf::StringVectorAttribute(multiView));
}

std::string ViewMetadata::defaultView() const
{
    if (hasView) return view;
    return multiView.empty() ? std::string() : multiView.front();
}

std::string ViewMetadata::channelView(const std::string& channel) const
{
    if (hasView) return view;
    // Older OpenEXR versions do not accept an empty vector here.
    return multiView.empty() ? std::string() : Imf::viewFromChannelName(channel, multiView);
}
