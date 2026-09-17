/**
 * Copyright (c) 2021 - 2023 Alban Fichet <alban dot fichet at gmx dot fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *  * Neither the name of the organization(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "LayerModel.h"

#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfChannelList.h>

#include <QImage>
#include <QIcon>
#include <functional>

namespace
{
    const LayerItem* findPreferredLayer(const LayerItem* root,
      const std::function<bool(const LayerItem*)>& accept = {})
    {
        static const LayerItem::LayerType priorities[] = {
          LayerItem::RGBA,
          LayerItem::RGB,
          LayerItem::YCA,
          LayerItem::YC,
          LayerItem::YA,
          LayerItem::Y,
        };

        for (LayerItem::LayerType type : priorities) {
            const LayerItem* item = root->child(type);
            if (item && (!accept || accept(item))) return item;
        }

        for (LayerItem* child : root->children()) {
            const LayerItem* item = findPreferredLayer(child, accept);
            if (item) return item;
        }

        return nullptr;
    }


    const LayerItem* findFirstChannel(const LayerItem* root)
    {
        switch (root->getType()) {
            case LayerItem::R:
            case LayerItem::G:
            case LayerItem::B:
            case LayerItem::A:
            case LayerItem::Y:
            case LayerItem::RY:
            case LayerItem::BY:
            case LayerItem::GENERAL:
                if (root->getPixelType() != Imf::PixelType::NUM_PIXELTYPES)
                    return root;
                break;
            default:
                break;
        }

        for (LayerItem* child : root->children()) {
            const LayerItem* item = findFirstChannel(child);
            if (item) return item;
        }

        return nullptr;
    }


    const LayerItem*
    findChannel(const LayerItem* root, int part, const std::string& channelName)
    {
        if (
          root->getPart() == part && root->getOriginalFullName() == channelName
          && root->getPixelType() != Imf::PixelType::NUM_PIXELTYPES) {
            return root;
        }

        for (LayerItem* child : root->children()) {
            const LayerItem* result = findChannel(child, part, channelName);
            if (result) return result;
        }

        return nullptr;
    }
}   // namespace


LayerModel::LayerModel(Imf::MultiPartInputFile& file, QObject* parent)
  : QAbstractItemModel(parent)
  , m_rootItem(new LayerItem(file))
  , m_fileHandle(file)
{
    const int nParts = file.parts();
    for (int part = 0; part < nParts; ++part) {
        m_views.push_back(ViewMetadata::read(file.header(part)));
        m_hasViews |= m_views.back().present();
    }
    if (!m_views.empty()) m_defaultView = m_views.front().defaultView();

    // To avoid having an extra item, we only add a root part for multipart files
    if (nParts > 1) {
        for (int part = 0; part < nParts; part++) {
            const Imf::Header& exrHeader = file.header(part);

            std::string partName = "Untitled part";

            if (exrHeader.hasName()) {
                partName = exrHeader.name();
            }

            LayerItem* leaf = m_rootItem->addLeaf(partName, nullptr, part);

            // Now list layers and add those to the part group
            const Imf::ChannelList& exrChannels = exrHeader.channels();

            for (Imf::ChannelList::ConstIterator it = exrChannels.begin();
                 it != exrChannels.end();
                 it++) {
                leaf->addLeaf(it.name(), &it.channel(), part);
            }
        }
    } else {
        const Imf::Header& exrHeader = file.header(0);

        // Now list layers and add those to the file group
        const Imf::ChannelList& exrChannels = exrHeader.channels();

        for (Imf::ChannelList::ConstIterator it = exrChannels.begin();
             it != exrChannels.end();
             it++) {
            m_rootItem->addLeaf(it.name(), &it.channel());
        }
    }

    m_rootItem->groupLayers();
}


LayerModel::~LayerModel() = default;


const LayerItem* LayerModel::defaultDisplayLayer() const
{
    const LayerItem* preferred = nullptr;
    if (!m_defaultView.empty()) {
        preferred = findPreferredLayer(m_rootItem.get(), [this](const LayerItem* item) {
            std::string view;
            return layerView(item, view) && view == m_defaultView;
        });
    }
    if (!preferred) preferred = findPreferredLayer(m_rootItem.get());
    if (preferred && (preferred->getType() == LayerItem::YC
                      || preferred->getType() == LayerItem::YCA)) {
        const auto& channels = m_fileHandle.header(preferred->getPart()).channels();
        const auto sampling = [&](LayerItem::LayerType type) {
            const auto* child = preferred->child(type);
            const auto* channel = child ? channels.findChannel(child->getOriginalFullName()) : nullptr;
            return channel ? Imath::V2i(channel->xSampling, channel->ySampling) : Imath::V2i(1);
        };
        const auto ry = sampling(LayerItem::RY);
        if (sampling(LayerItem::Y) != Imath::V2i(1)
            || sampling(LayerItem::A) != Imath::V2i(1)
            || sampling(LayerItem::BY) != ry
            || (ry != Imath::V2i(1) && ry != Imath::V2i(2)))
            return preferred->child(LayerItem::Y);
    }
    if (preferred) return preferred;

    // Search all standard layers before falling back to a single channel.
    return findFirstChannel(m_rootItem.get());
}


const LayerItem*
LayerModel::findChannel(int part, const std::string& channelName) const
{
    return ::findChannel(m_rootItem.get(), part, channelName);
}


bool LayerModel::layerView(const LayerItem* item, std::string& view) const
{
    if (item->getPixelType() != Imf::PixelType::NUM_PIXELTYPES) {
        view = m_views[item->getPart()].channelView(item->getOriginalFullName());
        return true;
    }
    bool found = false;
    for (const auto* child : item->children()) {
        std::string childView;
        if (!layerView(child, childView) || (found && view != childView)) return false;
        view = childView;
        found = true;
    }
    return found;
}

QString LayerModel::viewLabel(const LayerItem* item) const
{
    if (!m_hasViews || !item) return {};
    std::string view;
    if (!layerView(item, view)) return {}; // Mixed-view containers have no single label.
    if (view.empty()) return tr(" [No view]");
    return view == m_defaultView ? tr(" [%1, default]").arg(QString::fromStdString(view))
                                : QString(" [%1]").arg(QString::fromStdString(view));
}

LayerModel::StereoLayers LayerModel::stereoLayers(ResolutionLevel level) const
{
    StereoLayers result;
    const auto isColor = [](const LayerItem* item) {
        return item && (item->getType() == LayerItem::RGB || item->getType() == LayerItem::RGBA
          || item->getType() == LayerItem::YA || item->getType() == LayerItem::YC
          || item->getType() == LayerItem::YCA
          || (item->getType() == LayerItem::Y && item->getPixelType() != Imf::NUM_PIXELTYPES));
    };
    // The first real channel supplies the source layer path; part names are irrelevant.
    const auto path = [this](const LayerItem* color) -> std::string {
        const auto* channel = color->getType() == LayerItem::Y ? color : color->child(0);
        const auto& views = m_views[channel->getPart()];
        std::string name = channel->getOriginalFullName();
        auto dot = name.find_last_of('.');
        if (dot == std::string::npos) return std::string();
        name.erase(dot);
        if (!views.hasView && !views.channelView(channel->getOriginalFullName()).empty()) {
            dot = name.find_last_of('.');
            name = dot == std::string::npos ? std::string() : name.substr(0, dot);
        }
        return name;
    };
    const auto* preferred = defaultDisplayLayer();
    std::array<int, 2> counts = {{0, 0}};
    if (m_hasViews && isColor(preferred)) {
        const auto family = path(preferred);
        std::function<void(const LayerItem*)> visit = [&](const LayerItem* item) {
            if (isColor(item)) {
                std::string view;
                if (path(item) == family && layerView(item, view)) {
                    const int eye = view == "left" ? 0 : view == "right" ? 1 : -1;
                    if (eye >= 0) {
                        ++counts[eye];
                        result.eyes[eye] = item;
                    }
                }
                return;
            }
            for (const auto* child : item->children()) visit(child);
        };
        visit(m_rootItem.get());
    }
    for (size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] == 1) continue;
        result.eyes[i] = nullptr;
        const QString eye = i == 0 ? tr("left") : tr("right");
        result.eyeErrors[i] = counts[i] ? tr("Multiple %1 color layers match; stereo pairing is ambiguous.").arg(eye)
                                       : tr("No matching %1 color layer.").arg(eye);
        if (result.anaglyphError.isEmpty()) result.anaglyphError = result.eyeErrors[i];
    }
    if (result.anaglyphError.isEmpty()) {
        const auto& left = m_fileHandle.header(result.eyes[0]->getPart());
        const auto& right = m_fileHandle.header(result.eyes[1]->getPart());
        if (!ViewMetadata::stereoGeometryMatches(left, right, level))
            result.anaglyphError = tr("Stereo views require identical display windows and pixel aspect ratios.");
    }
    return result;
}

QVariant LayerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    LayerItem* item = static_cast<LayerItem*>(index.internalPointer());

    // clang-format off
    switch (role) {
        case Qt::DecorationRole:
            switch(index.column()) {
                case LAYER:
                    switch(item->getType()) {
                        case LayerItem::R:
                        case LayerItem::G:
                        case LayerItem::B:
                        case LayerItem::A:
                        case LayerItem::Y:
                        case LayerItem::RY:
                        case LayerItem::BY:
                        case LayerItem::GENERAL:
                            return QIcon(":/svg/038-image.svg");

                        case LayerItem::RGB:
                        case LayerItem::RGBA:
                        case LayerItem::YA:
                        case LayerItem::YC:
                        case LayerItem::YCA:
                            return QIcon(":/svg/090-archive-2.svg");

                        case LayerItem::GROUP:
                        case LayerItem::PART:
                            return QIcon(":/svg/100-folder-27.svg");

                        // This shall never happen but avoids warning message from compiler
                        case LayerItem::N_LAYERTYPES:
                            return QVariant();
                    }

                default:
                    return QVariant();
            }
            break;

        case Qt::DisplayRole:
            switch(index.column()) {
                case LAYER:
                    return QString::fromStdString(item->getLeafName()) + viewLabel(item);

                case TYPE:
                    switch(item->getType()) {
                        case LayerItem::R:       return tr("Red");
                        case LayerItem::G:       return tr("Green");
                        case LayerItem::B:       return tr("Blue");
                        case LayerItem::A:       return tr("Alpha");
                        case LayerItem::Y:       return tr("Luminance");
                        case LayerItem::RY:      return tr("Chroma R");
                        case LayerItem::BY:      return tr("Chroma B");
                        case LayerItem::RGB:     return tr("RGB");
                        case LayerItem::RGBA:    return tr("RGBA");
                        case LayerItem::YA:      return tr("Luminance Alpha");
                        case LayerItem::YC:      return tr("Luminance Chroma");
                        case LayerItem::YCA:     return tr("Luminance Chroma Alpha");
                        case LayerItem::GROUP:   return tr("Group");
                        case LayerItem::PART:    return tr("Part") + " " + QString::number(item->getPart());
                        case LayerItem::GENERAL: return tr("Framebuffer");
                        default: return QVariant();
                    }
                case PIXELTYPE:
                    switch(item->getPixelType()) {
                        case Imf::PixelType::UINT:  return tr("uint32");
                        case Imf::PixelType::HALF:  return tr("half");
                        case Imf::PixelType::FLOAT: return tr("float");
                        default: return QVariant();
                    }
                default:
                    return QVariant();
            }
            break;

        case Qt::ToolTipRole:
            {
                QString tooltip = "";
                tooltip += "<b>Part ID:</b> " + QString::number(item->getPart());

                // When it is a layer group, we do not want to display an empty layer name
                if (item->getType() != LayerItem::GROUP && item->getType() != LayerItem::PART) {
                    tooltip += "<br/>";
                    tooltip += "<b>Layer name:</b> " + QString::fromStdString(item->getOriginalFullName());
                }
                tooltip += viewLabel(item).toHtmlEscaped();
                return tooltip;
            }
        default:
            return QVariant();
    }
    // clang-format on

    return QVariant();
}


Qt::ItemFlags LayerModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return QAbstractItemModel::flags(index);
}


QVariant
LayerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case LAYER:
                return "Layer";
            case TYPE:
                return "Type";
            case PIXELTYPE:
                return "Pixel Type";
            default:
                return QVariant();
        }
    }

    return QVariant();
}


QModelIndex
LayerModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) return QModelIndex();

    LayerItem* parentItem;

    if (!parent.isValid()) {
        parentItem = m_rootItem.get();
    } else {
        parentItem = static_cast<LayerItem*>(parent.internalPointer());
    }

    LayerItem* childItem = parentItem->child(row);

    if (childItem) {
        return createIndex(row, column, childItem);
    }

    return QModelIndex();
}


QModelIndex LayerModel::parent(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QModelIndex();
    }

    LayerItem* childItem  = static_cast<LayerItem*>(index.internalPointer());
    LayerItem* parentItem = childItem->parentItem();

    if (parentItem == m_rootItem.get()) {
        return QModelIndex();
    }

    int row = 0;   //parentItem->row();
    return createIndex(row, 0, parentItem);
}


int LayerModel::rowCount(const QModelIndex& parent) const
{
    LayerItem* parentItem;

    if (parent.column() > 0) {
        return 0;
    }

    if (!parent.isValid()) {
        parentItem = m_rootItem.get();
    } else {
        parentItem = static_cast<LayerItem*>(parent.internalPointer());
    }

    return parentItem->childCount();
}


int LayerModel::columnCount(const QModelIndex&) const
{
    return N_LAYER_INFO;
}
