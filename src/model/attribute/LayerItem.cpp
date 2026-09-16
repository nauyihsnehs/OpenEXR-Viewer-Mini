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

#include "LayerItem.h"
#include <util/ViewMetadata.h>

#include <cassert>
#include <utility>

#include <QString>

#include <ImfMultiPartInputFile.h>
#include <ImfHeader.h>

LayerItem::LayerItem(
  Imf::MultiPartInputFile& file,
  LayerItem*               pParent,
  const std::string&       leafName,
  const std::string&       originalChannelName,
  const Imf::Channel*      pChannel,
  int                      part)
  : m_pParentItem(pParent)
  , m_part(part)
  , m_rootName("")
  , m_leafName(leafName)
  , m_channelName(originalChannelName)
  , m_fileHandle(file)
  , m_pixelType(pChannel ? pChannel->type : Imf::PixelType::NUM_PIXELTYPES)
{
    if (pParent) {
        m_rootName = pParent->getFullName();
    }

    m_type = constructType();
}

LayerItem::~LayerItem() = default;


LayerItem* LayerItem::addChild(
  const std::string&  leafName,
  const std::string&  originalChannelName,
  const Imf::Channel* channel,
  int                 part)
{
    std::unique_ptr<LayerItem> child(new LayerItem(
      m_fileHandle,
      this,
      leafName,
      originalChannelName,
      channel,
      part));
    LayerItem*                 result = child.get();
    m_childItems.push_back(std::move(child));

    return result;
}

LayerItem* LayerItem::addLeaf(
  const std::string& channelName, const Imf::Channel* pChannel, int part)
{
    QStringList channelHierachy
      = QString::fromStdString(channelName).split(".");

    LayerItem* pLeafPtr = this;

    for (auto& leafName : channelHierachy) {
        LayerItem* pExistingLeaf = pLeafPtr->child(leafName.toStdString());

        if (pExistingLeaf != nullptr) {
            pLeafPtr = pExistingLeaf;
        } else {
            pLeafPtr
              = pLeafPtr->addChild(leafName.toStdString(), "", nullptr, part);
        }
    }

    // Sanity check
    if (pLeafPtr->hasChannel()) {
        std::cerr << "The leaf is already populated with a framebuffer!"
                  << std::endl;
        std::cerr << "Leaf dump:" << std::endl
                  << "----------" << std::endl
                  << "channel name: " << pLeafPtr->m_channelName << std::endl
                  << "root name:    " << pLeafPtr->m_rootName << std::endl
                  << "leaf name:    " << pLeafPtr->m_leafName << std::endl;

        assert(0);
    }

    // Saves the original full channel name
    pLeafPtr->m_channelName = channelName;
    pLeafPtr->m_pixelType
      = pChannel ? pChannel->type : Imf::PixelType::NUM_PIXELTYPES;

    // Determine channel type based on the leaf name
    pLeafPtr->m_type = pLeafPtr->constructType();

    return pLeafPtr;
}


void LayerItem::groupLayers()
{
    struct GroupRule {
        LayerType   groupType;
        const char* name;
        LayerType   channels[4];
        int         channelCount;
    };

    static const GroupRule rules[] = {
      {RGBA, "RGBA", {R, G, B, A}, 4},
      {RGB, "RGB", {R, G, B, N_LAYERTYPES}, 3},
      {YCA, "YCA", {Y, RY, BY, A}, 4},
      {YC, "YC", {Y, RY, BY, N_LAYERTYPES}, 3},
      {YA, "YA", {Y, A, N_LAYERTYPES, N_LAYERTYPES}, 2},
    };

    for (const GroupRule& rule : rules) {
        bool matches = true;

        for (int i = 0; i < rule.channelCount; i++) {
            if (!hasChildLeaf(rule.channels[i])) {
                matches = false;
                break;
            }
        }

        if (!matches) continue;

        LayerItem*  firstChannel = child(rule.channels[0]);
        const auto views = ViewMetadata::read(m_fileHandle.header(firstChannel->getPart()));
        for (int i = 1; i < rule.channelCount; ++i) {
            const auto* channel = child(rule.channels[i]);
            if (channel->getPart() != firstChannel->getPart()
                || views.channelView(channel->m_channelName)
                     != views.channelView(firstChannel->m_channelName)) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        std::string layerName    = firstChannel->m_channelName;
        if (!layerName.empty()) layerName.erase(layerName.size() - 1);

        std::unique_ptr<LayerItem> group(new LayerItem(
          m_fileHandle,
          this,
          rule.name,
          layerName,
          nullptr,
          m_part));
        LayerItem*                 groupItem = group.get();

        for (int i = 0; i < rule.channelCount; i++) {
            std::unique_ptr<LayerItem> channel = takeChild(rule.channels[i]);
            channel->m_pParentItem             = groupItem;
            groupItem->m_childItems.push_back(std::move(channel));
        }

        m_childItems.push_back(std::move(group));
        break;
    }

    for (const std::unique_ptr<LayerItem>& item : m_childItems) {
        if (
          item->m_type != RGBA && item->m_type != RGB && item->m_type != YCA
          && item->m_type != YC && item->m_type != YA) {
            item->groupLayers();
        }
    }
}


HeaderItem* LayerItem::constructItemHierarchy(
  HeaderItem* parent, const std::string& partName, int partID)
{
    if (m_childItems.size() == 0) {
        // This is a terminal leaf
        assert(hasChannel());

        QString type = "framebuffer";
        switch (m_pixelType) {
            case Imf::PixelType::UINT:
                type += " (uint32)";
                break;
            case Imf::PixelType::HALF:
                type += " (half)";
                break;
            case Imf::PixelType::FLOAT:
                type += " (float)";
                break;
            default:
                break;
        }

        return new HeaderItem(
          parent,
          {QString::fromStdString(m_leafName), "", type},
          QString::fromStdString(partName),
          partID,
          QString::fromStdString(m_leafName),
          this);
    }

    HeaderItem* currRoot = nullptr;

    // Avoid empty root on top level
    if (m_pParentItem) {
        currRoot = new HeaderItem(
          parent,
          {QString::fromStdString(m_leafName),
           (int)childCount(),
           "virtual channel group"},
          QString::fromStdString(partName),
          partID,
          QString::fromStdString(m_leafName));
    } else {
        currRoot = parent;
    }

    if (hasChannel()) {
        // It's a leaf...
        // Both are valid but I prefer the nested representation
        // OpenEXRItem* leafNode = new OpenEXRItem(parent, {m_rootName, "",
        // "framebuffer"});

        QString type = "framebuffer";
        switch (m_pixelType) {
            case Imf::PixelType::UINT:
                type += " (uint32)";
                break;
            case Imf::PixelType::HALF:
                type += " (half)";
                break;
            case Imf::PixelType::FLOAT:
                type += " (float)";
                break;
            default:
                break;
        }

        new HeaderItem(
          currRoot,
          {".", "", type},
          QString::fromStdString(partName),
          partID,
          QString::fromStdString(m_leafName),
          this);
    }

    for (const std::unique_ptr<LayerItem>& item : m_childItems) {
        item->constructItemHierarchy(currRoot, partName, partID);
    }

    return currRoot;
}

/* ----------------------------------------------------------------------------
 * Child introspection and access functions
 * ------------------------------------------------------------------------- */

LayerItem* LayerItem::child(int index) const
{
    return m_childItems[index].get();
}


LayerItem* LayerItem::child(const std::string& name) const
{
    for (const std::unique_ptr<LayerItem>& item : m_childItems) {
        if (item->m_leafName == name) {
            return item.get();
        }
    }

    return nullptr;
}


LayerItem* LayerItem::child(const LayerType& type) const
{
    for (const std::unique_ptr<LayerItem>& item : m_childItems) {
        if (item->m_type == type) {
            return item.get();
        }
    }

    return nullptr;
}


int LayerItem::childIndex(const std::string& name) const
{
    for (size_t i = 0; i < m_childItems.size(); i++) {
        if (m_childItems[i]->m_leafName == name) {
            return i;
        }
    }

    return -1;
}


int LayerItem::childIndex(const LayerType& type) const
{
    for (size_t i = 0; i < m_childItems.size(); i++) {
        if (m_childItems[i]->m_type == type) {
            return i;
        }
    }

    return -1;
}


std::unique_ptr<LayerItem> LayerItem::takeChild(LayerType type)
{
    const int index = childIndex(type);
    assert(index >= 0);

    std::unique_ptr<LayerItem> result = std::move(m_childItems[index]);
    m_childItems.erase(m_childItems.begin() + index);

    return result;
}


std::vector<LayerItem*> LayerItem::children() const
{
    std::vector<LayerItem*> result;
    result.reserve(m_childItems.size());

    for (const std::unique_ptr<LayerItem>& item : m_childItems) {
        result.push_back(item.get());
    }

    return result;
}


int LayerItem::childCount() const
{
    return static_cast<int>(m_childItems.size());
}


bool LayerItem::hasChild(const std::string& name) const
{
    if (child(name) != nullptr) {
        return true;
    }

    return false;
}


bool LayerItem::hasChildLeaf(const std::string& name) const
{
    LayerItem* childItem = child(name);

    if (childItem != nullptr) {
        return childItem->hasChannel();
    }

    return false;
}


bool LayerItem::hasChildLeaf(const LayerType& type) const
{
    LayerItem* childItem = child(type);

    if (childItem != nullptr) {
        return childItem->hasChannel();
    }

    return false;
}


bool LayerItem::hasRGBChildLeafs() const
{
    return hasChildLeaf(R) && hasChildLeaf(G) && hasChildLeaf(B);
}


bool LayerItem::hasRGBAChildLeafs() const
{
    return hasRGBChildLeafs() && hasAChildLeaf();
}


bool LayerItem::hasYCChildLeafs() const
{
    return hasYChildLeaf() && hasChildLeaf(RY) && hasChildLeaf(BY);
}

bool LayerItem::hasYCAChildLeafs() const
{
    return hasYCChildLeafs() && hasAChildLeaf();
}

bool LayerItem::hasYChildLeaf() const
{
    return hasChildLeaf(Y);
}

bool LayerItem::hasYAChildLeafs() const
{
    return hasYChildLeaf() && hasAChildLeaf();
}

bool LayerItem::hasAChildLeaf() const
{
    return hasChildLeaf(A);
}

/* ----------------------------------------------------------------------------
 * Layer names
 * ------------------------------------------------------------------------- */

std::string LayerItem::getFullName() const
{
    if (m_pParentItem) {
        return m_pParentItem->getFullName() + "." + m_leafName;
    }

    return m_leafName;
}

std::string LayerItem::getLeafName() const
{
    return m_leafName;
}

std::string LayerItem::getOriginalFullName() const
{
    return m_channelName;
}

int LayerItem::getPart() const
{
    return (m_part == -1) ? 0 : m_part;
}


bool LayerItem::hasPartName() const
{
    // Single part file
    if (m_part == -1) {
        return m_fileHandle.header(0).hasName();
    }

    return m_fileHandle.header(m_part).hasName();
}


std::string LayerItem::getPartName() const
{
    // Single part file
    if (m_part == -1) {
        return m_fileHandle.header(0).name();
    }

    return m_fileHandle.header(m_part).name();
}


LayerItem::LayerType LayerItem::constructType()
{
    if (m_leafName == "R") {
        return R;
    } else if (m_leafName == "G") {
        return G;
    } else if (m_leafName == "B") {
        return B;
    } else if (m_leafName == "Y") {
        return Y;
    } else if (m_leafName == "A") {
        return A;
    } else if (m_leafName == "RY") {
        return RY;
    } else if (m_leafName == "BY") {
        return BY;
    } else if (m_leafName == "RGB") {
        return RGB;
    } else if (m_leafName == "RGBA") {
        return RGBA;
    } else if (m_leafName == "YA") {
        return YA;
    } else if (m_leafName == "YC") {
        return YC;
    } else if (m_leafName == "YCA") {
        return YCA;
    }

    // None of the above names but still holds a framebuffer
    if (hasChannel()) {
        return GENERAL;
    }

    if (m_pParentItem == nullptr) {
        return PART;
    }

    // If part id is not set to -1, it is a part
    // also, root item is either a part or a file made of multiple parts
    if (
      m_part != -1
      && (m_pParentItem == nullptr || m_pParentItem->m_pParentItem == nullptr)) {
        return PART;
    }

    return GROUP;
}




Imf::PixelType LayerItem::getPixelType() const
{
    return m_pixelType;
}
