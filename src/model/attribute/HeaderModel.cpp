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
#include <cstdint>
#include <cstring>
#include <sstream>
#include <OpenEXR/ImfIDManifest.h>

#include <QFileInfo>
#include <model/RadianceInput.h>


namespace {
template<class Value>
QString valueText(const Value& value)
{
    std::ostringstream text;
    text << value;
    return QString::fromStdString(text.str());
}

struct AttributeRows {
    const char* name;
    HeaderItem* parent;
    QString partName;
    int part;

    HeaderItem* child(HeaderItem* owner, const QVector<QVariant>& cells) const
    {
        return new HeaderItem(owner, cells, partName, part, name);
    }
    HeaderItem* root(const char* type, const QVariant& value = "") const
    {
        return child(parent, {name, value, type});
    }
};

template<class Attribute>
HeaderItem* formatAttribute(const AttributeRows& rows, const Attribute& attr)
{
    return rows.root(attr.typeName(), valueText(attr.value()));
}

template<class Attribute, class Size>
HeaderItem* boxRows(const AttributeRows& rows, const Attribute& attr,
                    const char* vectorType, const char* sizeType, Size width, Size height)
{
    auto* item = rows.root(attr.typeName());
    rows.child(item, {"min", valueText(attr.value().min), vectorType});
    rows.child(item, {"max", valueText(attr.value().max), vectorType});
    rows.child(item, {"width", valueText(width), sizeType});
    rows.child(item, {"height", valueText(height), sizeType});
    return item;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::Box2iAttribute& attr)
{
    const auto& box = attr.value();
    return boxRows(rows, attr, "vec2i", "int",
      int64_t(box.max.x) - box.min.x + 1, int64_t(box.max.y) - box.min.y + 1);
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::Box2fAttribute& attr)
{
    const auto& box = attr.value();
    return boxRows(rows, attr, "vec2f", "float",
      box.max.x - box.min.x + 1, box.max.y - box.min.y + 1);
}

template<class Attribute>
HeaderItem* matrixRows(const AttributeRows& rows, const Attribute& attr, int size)
{
    std::ostringstream text;
    for (int y = 0; y < size; ++y) {
        if (y) text << '\n';
        for (int x = 0; x < size; ++x) {
            if (x) text << '\t';
            text << attr.value()[y][x];
        }
    }
    auto* item = rows.root(attr.typeName());
    rows.child(item, {"", QString::fromStdString(text.str()), attr.typeName()});
    return item;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::M33fAttribute& attr)
{
    return matrixRows(rows, attr, 3);
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::M33dAttribute& attr)
{
    return matrixRows(rows, attr, 3);
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::M44fAttribute& attr)
{
    return matrixRows(rows, attr, 4);
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::M44dAttribute& attr)
{
    return matrixRows(rows, attr, 4);
}

QVariant vectorValue(float value) { return value; }
QVariant vectorValue(const std::string& value) { return QString::fromStdString(value); }

template<class Attribute>
HeaderItem* vectorRows(const AttributeRows& rows, const Attribute& attr, const char* valueType)
{
    auto* item = rows.root(attr.typeName(), valueText(attr.value().size()));
    size_t index = 0;
    for (const auto& value : attr.value()) {
        const QString label = QString::fromUtf8(rows.name) + "[" + valueText(index++) + "]";
        rows.child(item, {label, vectorValue(value), valueType});
    }
    return item;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::FloatVectorAttribute& attr)
{
    return vectorRows(rows, attr, Imf::FloatAttribute::staticTypeName());
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::StringVectorAttribute& attr)
{
    return vectorRows(rows, attr, Imf::StringAttribute::staticTypeName());
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::ChromaticitiesAttribute& attr)
{
    HeaderItem* attrItem = rows.root(Imf::ChromaticitiesAttribute::staticTypeName(), "");
    const QString sRed = valueText(attr.value().red);

    rows.child(attrItem, {"red", sRed, "vec2f"});
    const QString sGreen = valueText(attr.value().green);

    rows.child(attrItem, {"green", sGreen, "vec2f"});
    const QString sBlue = valueText(attr.value().blue);

    rows.child(attrItem, {"blue", sBlue, "vec2f"});
    const QString sWhite = valueText(attr.value().white);

    rows.child(attrItem, {"white", sWhite, "vec2f"});

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::CompressionAttribute& attr)
{
    std::stringstream ss;
    switch (attr.value()) {
        case Imf::Compression::NO_COMPRESSION:
            ss << "No compression";
            break;
        case Imf::Compression::RLE_COMPRESSION:
            ss << "RLE (lossless - run length encoding)";
            break;
        case Imf::Compression::ZIPS_COMPRESSION:
            ss << "ZIPS (lossless - zlib compression, one scan line at a time)";
            break;
        case Imf::Compression::ZIP_COMPRESSION:
            ss << "ZIP (lossless - zlib compression, in blocks of 16 scan "
                  "lines)";
            break;
        case Imf::Compression::PIZ_COMPRESSION:
            ss << "PIZ (lossless - piz-based wavelet compression)";
            break;
        case Imf::Compression::PXR24_COMPRESSION:
            ss << "PXR24 (lossy - 24-bit float compression)";
            break;
        case Imf::Compression::B44_COMPRESSION:
            ss << "B44 (lossy - 4-by-4 pixel block compression)";
            break;
        case Imf::Compression::B44A_COMPRESSION:
            ss << "B44A (lossy - 4-by-4 pixel block compression)";
            break;
        case Imf::Compression::DWAA_COMPRESSION:
            ss << "DWAA (lossy - DCT based compression, in blocks of 32 "
                  "scanlines)";
            break;
        case Imf::Compression::DWAB_COMPRESSION:
            ss << "DWAB (lossy - DCT based compression, in blocks of 256 "
                  "scanlines)";
            break;
        default:
            ss << "unknown compression type: " << attr.value();
            break;
    }
    HeaderItem* attrItem = rows.root(Imf::CompressionAttribute::staticTypeName(), ss.str().c_str());

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::DeepImageStateAttribute& attr)
{
    std::stringstream ss;
    switch (attr.value()) {
        case Imf::DeepImageState::DIS_MESSY:
            ss << "messy";
            break;
        case Imf::DeepImageState::DIS_SORTED:
            ss << "sorted";
            break;
        case Imf::DeepImageState::DIS_NON_OVERLAPPING:
            ss << "non overlapping";
            break;
        case Imf::DeepImageState::DIS_TIDY:
            ss << "tidy";
            break;
        default:
            ss << "unknown deepimage state: " << attr.value();
            break;
    }

    HeaderItem* attrItem = rows.root(Imf::DeepImageStateAttribute::staticTypeName(), ss.str().c_str());

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::EnvmapAttribute& attr)
{
    std::stringstream ss;
    switch (attr.value()) {
        case Imf::Envmap::ENVMAP_LATLONG:
            ss << "Latitude-longitude environment map";
            break;
        case Imf::Envmap::ENVMAP_CUBE:
            ss << "Cube map";
            break;
        default:
            ss << "unknown envmap parametrization: " << attr.value();
            break;
    }

    HeaderItem* attrItem = rows.root(Imf::EnvmapAttribute::staticTypeName(), ss.str().c_str());

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::IDManifestAttribute& attr)
{
    HeaderItem* attrItem = rows.root(Imf::IDManifestAttribute::staticTypeName(), "");

    Imf::IDManifest manifest(attr.value());

    for (size_t i = 0; i < manifest.size(); i++) {
        std::stringstream sI;
        sI << rows.name << "[" << i << "]";

        const Imf::IDManifest::ChannelGroupManifest chManifest = manifest[i];

        HeaderItem* manifestGroup = rows.child(attrItem, {sI.str().c_str(), "", "ChannelGroupManifest"});

        HeaderItem* manifestGroupChannels = rows.child(manifestGroup, {"channels", "", ""});

        for (const auto& ch : chManifest.getChannels()) {
            rows.child(manifestGroupChannels, {"", ch.c_str(), Imf::StringAttribute::staticTypeName()});
        }

        const std::vector<std::string>& components = chManifest.getComponents();

        HeaderItem* manifestGroupComponents = rows.child(manifestGroup, {"components",
           QString::number(components.size()),
           Imf::StringVectorAttribute::staticTypeName()});

        for (size_t componentIndex = 0; componentIndex < components.size();
             componentIndex++) {
            std::stringstream componentName;
            componentName << "component[" << componentIndex << "]";

            rows.child(manifestGroupComponents, {componentName.str().c_str(),
               components[componentIndex].c_str(),
               Imf::StringAttribute::staticTypeName()});
        }

        const char* lifetime = "unknown";
        switch (chManifest.getLifetime()) {
            case Imf::IDManifest::LIFETIME_FRAME:
                lifetime = "frame";
                break;
            case Imf::IDManifest::LIFETIME_SHOT:
                lifetime = "shot";
                break;
            case Imf::IDManifest::LIFETIME_STABLE:
                lifetime = "stable";
                break;
        }

        rows.child(manifestGroup, {"lifetime", lifetime, "Imf::IDManifest::IdLifetime"});

        rows.child(manifestGroup, {"hashScheme",
           chManifest.getHashScheme().c_str(),
           Imf::StringAttribute::staticTypeName()});

        rows.child(manifestGroup, {"encodingScheme",
           chManifest.getEncodingScheme().c_str(),
           Imf::StringAttribute::staticTypeName()});
    }

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::KeyCodeAttribute& attr)
{
    HeaderItem* attrItem = rows.root(Imf::KeyCodeAttribute::staticTypeName(), "");

    rows.child(attrItem, {"Film NFC code", attr.value().filmMfcCode(), "int"});

    rows.child(attrItem, {"Film type", attr.value().filmType(), "int"});

    rows.child(attrItem, {"Prefix", attr.value().prefix(), "int"});

    rows.child(attrItem, {"Count", attr.value().count(), "int"});

    rows.child(attrItem, {"Perf offset", attr.value().perfOffset(), "int"});

    rows.child(attrItem, {"Prefs per frame", attr.value().perfsPerFrame(), "int"});

    rows.child(attrItem, {"Perfs per count", attr.value().perfsPerCount(), "int"});

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::LineOrderAttribute& attr)
{
    std::stringstream ss;
    switch (attr.value()) {
        case Imf::LineOrder::INCREASING_Y:
            ss
              << "Increasing Y";   //: first scan line has lowest y coordinate";
            break;
        case Imf::LineOrder::DECREASING_Y:
            ss
              << "Decreasing Y";   //: first scan line has highest y coordinate";
            break;
        case Imf::LineOrder::RANDOM_Y:
            ss << "Random Y";   //: tiles are written in random order";
            break;              // Only for tiled
        default:
            ss << "unknown line order: " << attr.value();
            break;
    }
    HeaderItem* attrItem = rows.root(Imf::LineOrderAttribute::staticTypeName(), ss.str().c_str());

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::PreviewImageAttribute& attr)
{
    std::stringstream ss;

    ss << attr.value().width() << "x" << attr.value().height() << std::endl;

    HeaderItem* attrItem = rows.root(Imf::PreviewImageAttribute::staticTypeName(), ss.str().c_str());

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::RationalAttribute& attr)
{
    std::stringstream ss;

    // n/d                for d > 0
    // positive infinity  for n > 0, d == 0
    // negative infinity  for n < 0, d == 0
    // not a number (NaN) for n == 0, d == 0

    const int          n = attr.value().n;
    const unsigned int d = attr.value().d;

    if (d == 0) {
        if (n > 0) {
            ss << "+inf";
        } else if (n < 0) {
            ss << "-inf";
        } else {
            ss << "NaN";
        }
    } else {
        ss << n << "/" << d;
    }

    HeaderItem* attrItem = rows.root(Imf::RationalAttribute::staticTypeName(), ss.str().c_str());

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::TileDescriptionAttribute& attr)
{
    HeaderItem* attrItem = rows.root(Imf::TileDescriptionAttribute::staticTypeName(), "");

    rows.child(attrItem, {"xSize", attr.value().xSize, "unsigned int"});

    rows.child(attrItem, {"ySize", attr.value().ySize, "unsigned int"});

    switch (attr.value().mode) {
        case Imf::ONE_LEVEL:
            rows.child(attrItem, {"mode", "one level", "Imf::LevelMode"});
            break;
        case Imf::MIPMAP_LEVELS:
            rows.child(attrItem, {"mode", "mipmap levels", "Imf::LevelMode"});
            break;
        case Imf::RIPMAP_LEVELS:
            rows.child(attrItem, {"mode", "ripmap levels", "Imf::LevelMode"});
            break;
        case Imf::NUM_LEVELMODES:
            rows.child(attrItem, {"mode", "unknown", "Imf::LevelMode"});
            break;
    }

    switch (attr.value().roundingMode) {
        case Imf::ROUND_DOWN:
            rows.child(attrItem, {"roundingMode", "round down", "Imf::LevelRoundingMode"});
            break;
        case Imf::ROUND_UP:
            rows.child(attrItem, {"roundingMode", "round up", "Imf::LevelRoundingMode"});
            break;
        case Imf::NUM_ROUNDINGMODES:
            rows.child(attrItem, {"roundingMode", "unknown", "Imf::LevelRoundingMode"});
            break;
    }

    return attrItem;
}

HeaderItem* formatAttribute(const AttributeRows& rows, const Imf::TimeCodeAttribute& attr)
{
    std::stringstream ss;
    ss << attr.value().hours() << ":" << attr.value().minutes() << ":"
       << attr.value().seconds() << " f" << attr.value().frame();

    HeaderItem* attrItem = rows.root(Imf::TimeCodeAttribute::staticTypeName(), ss.str().c_str());

    rows.child(attrItem, {"hours", attr.value().hours(), "int"});

    rows.child(attrItem, {"minutes", attr.value().minutes(), "int"});

    rows.child(attrItem, {"seconds", attr.value().seconds(), "int"});

    rows.child(attrItem, {"frame", attr.value().frame(), "int"});

    rows.child(attrItem, {"colorFrame", attr.value().colorFrame() ? "yes" : "no", "bool"});

    rows.child(attrItem, {"fieldPhase", attr.value().fieldPhase() ? "yes" : "no", "bool"});

    rows.child(attrItem, {"bgf0", attr.value().bgf0() ? "yes" : "no", "bool"});

    rows.child(attrItem, {"bgf1", attr.value().bgf1() ? "yes" : "no", "bool"});

    rows.child(attrItem, {"bgf2", attr.value().bgf2() ? "yes" : "no", "bool"});

    for (int group = 1; group <= 8; ++group)
        rows.child(attrItem, {QString("binaryGroup%1").arg(group), attr.value().binaryGroup(group), "int"});

    return attrItem;
}

} // namespace

HeaderModel::HeaderModel(
  const ExrInput& file, int n_parts, QObject* parent)
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
  const ExrInput& file, const QString& filename)
{
    QString rootValue = QString::number(file.parts()) + " part";
    if (file.parts() > 1) {
        rootValue += "s";
    }

    HeaderItem* fileRoot = new HeaderItem(
      m_rootItem,
      {QFileInfo(filename).fileName(), rootValue, "file"});

    const int nParts = file.parts();
    if (file.radiance) {
        new HeaderItem(fileRoot, {tr("Format"), "Radiance RGBE", "format"});
        new HeaderItem(fileRoot, {tr("Resolution"), file.radiance->resolutionLine(), "text"});
        for (const QString& line : file.radiance->headerLines()) {
            const int separator = line.indexOf('=');
            if (separator < 0)
                new HeaderItem(fileRoot, {tr("Header"), line, "text"});
            else
                new HeaderItem(fileRoot, {line.left(separator).trimmed(), line.mid(separator + 1).trimmed(), "text"});
        }
        addItem("channels", file.header(0)["channels"], fileRoot, QString(), 0);
        return;
    }

    if (nParts > 1) {
        for (int i = 0; i < nParts; i++) {
            const Imf::Header& exrHeader = file.header(i);

            std::string partName = tr("Part %1").arg(i).toStdString();

            if (exrHeader.hasName() && !exrHeader.name().empty()) {
                partName = exrHeader.name();
            }

            QString     partValue = "[" + QString::number(i) + "]";
            HeaderItem* partRoot
              = new HeaderItem(fileRoot, {partName.c_str(), partValue, "part"},
                               QString::fromStdString(partName), i);

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


QModelIndex HeaderModel::partIndex(int part) const
{
    if (part < 0 || part >= m_fileHandle.parts()) return {};
    const auto file = index(0, 0);
    return m_fileHandle.parts() == 1 ? file : index(part, 0, file);
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
        const auto& typedAttr = Imf::_class::cast(_attribute);                        \
        return formatAttribute({_name, _parent, _partName, _partID}, typedAttr);         \
    }

HeaderItem* HeaderModel::addItem(
  const char*           name,
  const Imf::Attribute& attribute,
  HeaderItem*           parent,
  QString               partName,
  int                   part_number)
{
    if (std::strcmp(attribute.typeName(), Imf::ChannelListAttribute::staticTypeName()) == 0)
        return addItem(name, Imf::ChannelListAttribute::cast(attribute), parent, partName, part_number);
    // Try to see if we have a function to handle such attribute type
    // clang-format off
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, Box2iAttribute);
    CALL_FOR_CLASS(name, attribute, parent, partName, part_number, Box2fAttribute);
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

HeaderItem* HeaderModel::addItem(
  const char*                      name,
  const Imf::ChannelListAttribute& attr,
  HeaderItem*                      parent,
  QString                          partName,
  int                              part_number)
{
    HeaderItem* attrItem = new HeaderItem(parent);

    std::stringstream ss;

    // Channel List
    size_t channelCount = 0;

    // Sanity check
    if (m_partRootLayer[part_number]) {
        delete m_partRootLayer[part_number];
        m_partRootLayer[part_number] = nullptr;
    }

    m_partRootLayer[part_number] = new LayerItem(m_fileHandle);
    // TODO: add layer type
    for (Imf::ChannelList::ConstIterator chIt = attr.value().begin();
         chIt != attr.value().end();
         chIt++) {
        m_partRootLayer[part_number]->addLeaf(chIt.name(), &chIt.channel());

        ++channelCount;
    }

    ss << channelCount;

    m_partRootLayer[part_number]->constructItemHierarchy(
      attrItem,
      partName.toStdString(),
      part_number);


    QVector<QVariant> itemData = {
      name,
      QString(ss.str().c_str()),
      Imf::ChannelListAttribute::staticTypeName()};
    attrItem->setData(itemData);
    attrItem->setItemName(name);
    attrItem->setPartName(partName);
    attrItem->setPartID(part_number);

    return attrItem;
}

#undef CALL_FOR_CLASS
