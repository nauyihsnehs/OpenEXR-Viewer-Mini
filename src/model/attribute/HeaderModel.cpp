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

#include "HeaderModel.h"

#include <cassert>

#include <QFileInfo>

HeaderModel::HeaderModel(
  Imf::MultiPartInputFile& file, int n_parts, QObject* parent)
  : QAbstractItemModel(parent)
  , m_rootItem(new HeaderItem(nullptr, {tr("Name"), tr("Value"), tr("Type")}))
  , m_fileHandle(file)
{
    m_headerItems.resize(n_parts);
    m_partRootLayer.resize(n_parts);
}

HeaderModel::~HeaderModel()
{
    delete m_rootItem;

    for (LayerItem* it : m_partRootLayer) {
        delete it;
    }
}

void HeaderModel::addFile(
  const Imf::MultiPartInputFile& file, const QString& filename)
{
    QString rootValue = QString::number(file.parts()) + " part";
    if (file.parts() > 1) {
        rootValue += "s";
    }

    HeaderItem* fileRoot = new HeaderItem(
      m_rootItem,
      {QFileInfo(filename).fileName(), rootValue, "file"});

    const int nParts = file.parts();

    if (nParts > 1) {
        for (int i = 0; i < nParts; i++) {
            const Imf::Header& exrHeader = file.header(i);

            std::string partName = "Untitled part";

            if (exrHeader.hasName()) {
                partName = exrHeader.name();
            }

            QString     partValue = "[" + QString::number(i) + "]";
            HeaderItem* partRoot
              = new HeaderItem(fileRoot, {partName.c_str(), partValue, "part"});

            addHeader(exrHeader, partRoot, QString::fromStdString(partName), i);
        }
    } else if (nParts == 1) {
        const Imf::Header& exrHeader = file.header(0);

        std::string partName = "Untitled part";

        if (exrHeader.hasName()) {
            partName = exrHeader.name();
        }

        addHeader(exrHeader, fileRoot, QString::fromStdString(partName), 0);
    } else {
        delete fileRoot;
    }
}


QVariant HeaderModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (role != Qt::DisplayRole) {
        return QVariant();
    }

    HeaderItem* item = static_cast<HeaderItem*>(index.internalPointer());

    return item->data(index.column());
}

Qt::ItemFlags HeaderModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return QAbstractItemModel::flags(index);
}

QVariant HeaderModel::headerData(
  int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        return m_rootItem->data(section);
    }

    return QVariant();
}

QModelIndex
HeaderModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) return QModelIndex();

    HeaderItem* parentItem;

    if (!parent.isValid()) {
        parentItem = m_rootItem;
    } else {
        parentItem = static_cast<HeaderItem*>(parent.internalPointer());
    }

    HeaderItem* childItem = parentItem->child(row);

    if (childItem) {
        return createIndex(row, column, childItem);
    }

    return QModelIndex();
}

QModelIndex HeaderModel::parent(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QModelIndex();
    }

    HeaderItem* childItem  = static_cast<HeaderItem*>(index.internalPointer());
    HeaderItem* parentItem = childItem->parentItem();

    if (parentItem == m_rootItem) {
        return QModelIndex();
    }

    return createIndex(parentItem->row(), 0, parentItem);
}

int HeaderModel::rowCount(const QModelIndex& parent) const
{
    HeaderItem* parentItem;

    if (parent.column() > 0) {
        return 0;
    }

    if (!parent.isValid()) {
        parentItem = m_rootItem;
    } else {
        parentItem = static_cast<HeaderItem*>(parent.internalPointer());
    }

    return parentItem->childCount();
}

int HeaderModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return static_cast<HeaderItem*>(parent.internalPointer())
          ->columnCount();
    }

    return m_rootItem->columnCount();
}


void HeaderModel::addHeader(
  const Imf::Header& header,
  HeaderItem*        root,
  const QString      partName,
  int                partID)
{
    assert(partID < (int)m_headerItems.size());
    assert(partID < (int)m_partRootLayer.size());

    m_partRootLayer[partID] = nullptr;

    for (Imf::Header::ConstIterator it = header.begin(); it != header.end();
         it++) {
        addItem(it.name(), it.attribute(), root, partName, partID);
    }
}

#define CALL_FOR_CLASS(_name, _attribute, _parent, _partName, _partID, _class) \
    if (strcmp(_attribute.typeName(), Imf::_class::staticTypeName()) == 0) {   \
        auto typedAttr = Imf::_class::cast(_attribute);                        \
        return addItem(_name, typedAttr, _parent, _partName, _partID);         \
    }

HeaderItem* HeaderModel::addItem(
  const char*           name,
  const Imf::Attribute& attribute,
  HeaderItem*           parent,
  QString               partName,
  int                   part_number)
{
    // Try to see if we have a function to handle such attribute type
    // clang-format off
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, Box2iAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, Box2fAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, ChannelListAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, ChromaticitiesAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, CompressionAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, DeepImageStateAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, DoubleAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, EnvmapAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, FloatAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, FloatVectorAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, IDManifestAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, IntAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, KeyCodeAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, LineOrderAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, M33fAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, M33dAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, M44fAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, M44dAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, PreviewImageAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, RationalAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, StringAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, StringVectorAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, TileDescriptionAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, TimeCodeAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, V2iAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, V2fAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, V2dAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, V3iAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, V3fAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, V3dAttribute);
    // Opaque -> unknown
    // CALL_FOR_CLASS(name, attribute, parent, partName, part_number, OpaqueAttribute);
    // clang-format on

    // We've tried everything we knew so far... this is an unknown attribute
    HeaderItem* attrItem = new HeaderItem(
      parent,
      {name, "Unsupported", attribute.typeName()},
      partName,
      part_number,
      name);


    return attrItem;
}
