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

#pragma once

#include <model/attribute/HeaderItem.h>

#include <OpenEXR/ImfChannelListAttribute.h>

#include <QVariant>

#include <memory>
#include <string>
#include <vector>

class LayerItem
{
  public:
    enum LayerType
    {
        // Single channel
        R,
        G,
        B,
        A,
        Y,
        RY,
        BY,
        GENERAL,
        // Group Types,
        RGB,
        RGBA,
        YA,
        YC,
        YCA,
        GROUP,
        PART,
        N_LAYERTYPES
    };

    LayerItem(
      Imf::MultiPartInputFile& file,
      LayerItem*               pParent             = nullptr,
      const std::string&       leafName            = "",
      const std::string&       originalChannelName = "",
      const Imf::Channel*      pChannel            = nullptr,
      int                      part                = -1);

    ~LayerItem();

    LayerItem* addLeaf(
      const std::string&  channelName,
      const Imf::Channel* pChannel,
      int                 part = -1);


    // Perfoms the grouping of known layer groups: RGB, RGBA, YC, YCA...
    void groupLayers();

    HeaderItem* constructItemHierarchy(
      HeaderItem* parent, const std::string& partName, int partID);

    LayerItem* child(int index) const;
    LayerItem* child(const std::string& name) const;
    LayerItem* child(const LayerType& type) const;

    int childIndex(const std::string& name) const;
    int childIndex(const LayerType& type) const;

    int childCount() const;

    std::vector<LayerItem*> children() const;

    LayerItem* parentItem() { return m_pParentItem; }

    bool hasChild(const std::string& name) const;
    bool hasChildLeaf(const std::string& name) const;
    bool hasChildLeaf(const LayerType& type) const;

    bool hasRGBChildLeafs() const;
    bool hasRGBAChildLeafs() const;
    bool hasYCChildLeafs() const;
    bool hasYCAChildLeafs() const;
    bool hasYChildLeaf() const;
    bool hasYAChildLeafs() const;
    bool hasAChildLeaf() const;


    std::string getFullName() const;
    std::string getLeafName() const;
    std::string getOriginalFullName() const;
    int         getPart() const;

    bool        hasPartName() const;
    std::string getPartName() const;

    LayerType getType() const { return m_type; }

    Imf::PixelType getPixelType() const;

  private:
    LayerType  constructType();
    LayerItem* addChild(
      const std::string&  leafName,
      const std::string&  originalChannelName,
      const Imf::Channel* channel,
      int                 part);
    std::unique_ptr<LayerItem> takeChild(LayerType type);
    bool                       hasChannel() const
    {
        return m_pixelType != Imf::PixelType::NUM_PIXELTYPES;
    }

    std::vector<std::unique_ptr<LayerItem>> m_childItems;
    LayerItem*                              m_pParentItem;

    // Id of the part
    const int m_part;

    // Name of the root hierarchy (removes double '.')
    std::string m_rootName;

    // Name of the current leaf
    const std::string m_leafName;

    // Full original channel name
    std::string m_channelName;

    LayerType m_type;

    Imf::MultiPartInputFile& m_fileHandle;
    Imf::PixelType           m_pixelType;
};
