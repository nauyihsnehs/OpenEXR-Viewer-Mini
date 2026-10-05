#include "InspectorWidget.h"
#include "ViewerIcons.h"

#include <model/OpenEXRImage.h>
#include <util/ViewMetadata.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>
#include <set>

namespace {
enum { DescriptionRole = Qt::UserRole + 1, PreviewRole, ObjectIconRole };

LayerItem* layerItem(const QModelIndex& sourceIndex)
{
    return sourceIndex.isValid() ? static_cast<LayerItem*>(sourceIndex.internalPointer()) : nullptr;
}

bool isChannel(const LayerItem* item)
{
    return item->getPixelType() != Imf::NUM_PIXELTYPES;
}

bool isDisplayable(const LayerItem* item)
{
    return item && item->getType() != LayerItem::GROUP && item->getType() != LayerItem::PART
           && item->getType() != LayerItem::N_LAYERTYPES;
}

QString objectKind(const LayerItem* item)
{
    if (!item) return QObject::tr("File");
    if (isChannel(item)) return QObject::tr("Channel");
    if (item->getType() == LayerItem::PART) return QObject::tr("Part");
    if (item->getType() == LayerItem::GROUP) return QObject::tr("Group");
    return QObject::tr("Layer");
}

ViewerIcons::Kind objectIcon(const LayerItem* item)
{
    if (!item) return ViewerIcons::File;
    if (isChannel(item)) return ViewerIcons::Channel;
    if (item->getType() == LayerItem::PART) return ViewerIcons::Part;
    if (item->getType() == LayerItem::GROUP) return ViewerIcons::Group;
    return ViewerIcons::Layer;
}

QString partExplanation()
{
    return QObject::tr("A Part is an independent image within the EXR file, with its own channels and attributes.");
}

QString objectKey(const QModelIndex& index)
{
    const auto* item = layerItem(index);
    if (!item) return {};
    // Groups have no original channel name. Include the full hierarchy so that
    // two different groups in the same part never share an inspection identity.
    return QString("%1:%2:%3").arg(item->getPart()).arg(int(item->getType()))
      .arg(QString::fromStdString(item->getFullName()));
}

QString layerFullName(const LayerItem* item)
{
    QString name = QString::fromStdString(item->getOriginalFullName());
    if (name.isEmpty()) {
        name = QString::fromStdString(item->getFullName());
        if (name.startsWith('.')) name.remove(0, 1); // Omit the invisible root.
    } else if (!isChannel(item) && name.endsWith('.')) {
        name.chop(1); // Combined layers store their channel prefix with a final dot.
    }
    return name;
}

QString tooltipText(const QString& text)
{
    return "<qt>" + text.toHtmlEscaped().replace('\n', "<br/>") + "</qt>";
}

void visitTree(const QAbstractItemModel* model, const QModelIndex& parent,
               const std::function<void(const QModelIndex&)>& visit)
{
    if (!model) return;
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const auto index = model->index(row, 0, parent);
        visit(index);
        visitTree(model, index, visit);
    }
}

QString pixelType(Imf::PixelType type, bool bits = false)
{
    switch (type) {
        case Imf::HALF: return bits ? QStringLiteral("half · 16-bit") : QStringLiteral("half");
        case Imf::FLOAT: return bits ? QStringLiteral("float · 32-bit") : QStringLiteral("float");
        case Imf::UINT: return bits ? QStringLiteral("uint32 · 32-bit") : QStringLiteral("uint32");
        default: return {};
    }
}

void collectChannels(const LayerItem* item, QStringList& names, std::set<Imf::PixelType>& types)
{
    if (isChannel(item)) {
        names << QString::fromStdString(item->getOriginalFullName());
        types.insert(item->getPixelType());
    }
    for (const auto* child : item->children()) collectChannels(child, names, types);
}

QString formatTypes(const std::set<Imf::PixelType>& types, bool bits)
{
    QStringList result;
    for (const auto type : types) result << pixelType(type, bits);
    return types.size() > 1 ? QObject::tr("Mixed: %1").arg(result.join(", ")) : result.join(", ");
}

QString dimensions(const Imath::Box2i& box)
{
    return QStringLiteral("%1 × %2").arg(qint64(box.max.x) - box.min.x + 1)
      .arg(qint64(box.max.y) - box.min.y + 1);
}

QString windowText(const Imath::Box2i& box)
{
    return QStringLiteral("(%1, %2) – (%3, %4)")
      .arg(box.min.x).arg(box.min.y).arg(box.max.x).arg(box.max.y);
}

QLabel* textLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

void clearForm(QWidget* widget)
{
    if (!widget->layout()) return;
    while (auto* item = widget->layout()->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    delete widget->layout();
}

QFormLayout* newForm(QWidget* widget)
{
    clearForm(widget);
    auto* form = new QFormLayout(widget);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    return form;
}

void addField(QFormLayout* form, const QString& name, const QString& value)
{
    auto* label = textLabel(name, form->parentWidget());
    label->setObjectName("inspectorMuted");
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    form->addRow(label, textLabel(value, form->parentWidget()));
}

void addPathField(QFormLayout* form, OpenEXRImage* source)
{
    auto* row = new QWidget(form->parentWidget());
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    const QString path = source->isStream() ? QObject::tr("Stream") : source->getFilename();
    layout->addWidget(textLabel(path, row), 1);
    auto* copy = new QToolButton(row);
    ViewerIcons::setupButton(copy, ViewerIcons::Copy, QObject::tr("Copy file path"),
      source->isStream() ? QObject::tr("Streams have no file path to copy") : QObject::tr("Copy file path"));
    copy->setEnabled(!source->isStream());
    QObject::connect(copy, &QToolButton::clicked, row, [path] {
        QApplication::clipboard()->setText(path);
    });
    layout->addWidget(copy);
    auto* label = textLabel(QObject::tr("Path"), form->parentWidget());
    label->setObjectName("inspectorMuted");
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    form->addRow(label, row);
}

QString headerValue(OpenEXRImage* source, int part, const QString& name)
{
    const auto* model = source->getHeaderModel();
    const auto root = model->partIndex(part);
    for (int row = 0; row < model->rowCount(root); ++row) {
        if (model->index(row, 0, root).data().toString() == name)
            return model->index(row, 1, root).data().toString();
    }
    return {};
}

void addPartFields(QFormLayout* form, OpenEXRImage* source, int part)
{
    const auto& header = source->header(part);
    addField(form, QObject::tr("Dimensions"), dimensions(header.displayWindow()));
    addField(form, QObject::tr("Data window"), windowText(header.dataWindow()));
    addField(form, QObject::tr("Display window"), windowText(header.displayWindow()));
    addField(form, QObject::tr("Pixel aspect"), QString::number(header.pixelAspectRatio(), 'g', 8));
    if (!source->isRadiance()) {
        addField(form, QObject::tr("Compression"), headerValue(source, part, "compression"));
        addField(form, QObject::tr("Storage"), header.hasType() ? QString::fromStdString(header.type())
          : header.hasTileDescription() ? QObject::tr("Tiled") : QObject::tr("Scanline"));
    }
    int count = 0;
    for (auto it = header.channels().begin(); it != header.channels().end(); ++it) ++count;
    addField(form, QObject::tr("Channels"), QString::number(count));
}

QToolButton* foldButton(const QString& title, QWidget* body, QVBoxLayout* layout)
{
    auto* button = new QToolButton(layout->parentWidget());
    button->setObjectName("inspectorFold");
    button->setText(title);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setArrowType(Qt::RightArrow);
    button->setCheckable(true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    body->hide();
    QObject::connect(button, &QToolButton::toggled, body, [button, body](bool expanded) {
        button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(expanded);
    });
    layout->addWidget(button);
    layout->addWidget(body);
    return button;
}

// Retain child formatting (including matrix tabs/newlines) when inspecting a
// compound attribute whose own Value cell is empty.
QString attributeValue(const QModelIndex& sourceIndex)
{
    if (!sourceIndex.isValid()) return {};
    const auto* model = sourceIndex.model();
    QStringList lines;
    const auto value = sourceIndex.sibling(sourceIndex.row(), 1).data().toString();
    if (!value.isEmpty()) lines << value;
    visitTree(model, sourceIndex.sibling(sourceIndex.row(), 0), [&](const QModelIndex& child) {
        const auto name = child.data().toString();
        const auto text = child.sibling(child.row(), 1).data().toString();
        if (!text.isEmpty()) lines << (name.isEmpty() ? text : name + ": " + text);
    });
    return lines.join('\n');
}

// Paint on demand rather than caching a pixmap, so headers follow theme and DPI changes.
class ObjectBadge : public QWidget
{
  public:
    ObjectBadge(ViewerIcons::Kind kind, const QString& name, QWidget* parent)
      : QWidget(parent), m_icon(ViewerIcons::icon(kind))
    {
        setFixedSize(16, 16);
        setAccessibleName(name);
        setToolTip(name);
    }
  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        m_icon.paint(&painter, rect(), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
  private:
    QIcon m_icon;
};

class LayerDelegate : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return QSize(100, qMax(28, option.fontMetrics.height() + 8));
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem styled(option);
        initStyleOption(&styled, index);
        const QString name = styled.text;
        const QString description = index.data(DescriptionRole).toString();
        const int preview = index.data(PreviewRole).toInt();
        styled.text.clear();
        styled.icon = QIcon();
        styled.features.setFlag(QStyleOptionViewItem::HasDecoration, false);
        const auto* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &styled, painter, option.widget);
        painter->save();
        painter->setClipRect(option.rect);
        painter->setFont(styled.font);
        QRect area = option.rect.adjusted(4, 0, -6, 0);
        const auto group = (styled.state & QStyle::State_Enabled) ? QPalette::Active : QPalette::Disabled;
        // Inspector themes use the same text ink on their tinted selected rows.
        QColor color = styled.palette.color(group, QPalette::Text);
        const QIcon::Mode mode = (styled.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled;
        ViewerIcons::icon(static_cast<ViewerIcons::Kind>(index.data(ObjectIconRole).toInt()), color)
          .paint(painter, QRect(area.left(), area.center().y() - 8, 16, 16), Qt::AlignCenter, mode);
        area.adjust(24, 0, 0, 0);
        // Reserve one slot on every row so preview markers stay aligned.
        const QRect previewArea(area.right() - 15, area.center().y() - 8, 16, 16);
        area.adjust(0, 0, -24, 0);
        if (preview) ViewerIcons::icon(preview == 2 ? ViewerIcons::Stereo : ViewerIcons::Preview, color)
          .paint(painter, previewArea, Qt::AlignCenter, mode);
        const QFontMetrics metrics(styled.font);
        const int available = qMax(0, area.width());
        const int detailWidth = description.isEmpty() ? 0
          : qMin(available / 2, metrics.horizontalAdvance(description));
        const int nameWidth = qMax(0, available - detailWidth - (detailWidth ? 12 : 0));
        painter->setPen(color);
        painter->drawText(QRect(area.left(), area.top(), nameWidth, area.height()),
          Qt::AlignLeft | Qt::AlignVCenter,
          metrics.elidedText(name, Qt::ElideRight, nameWidth));
        color.setAlpha(170);
        painter->setPen(color);
        painter->drawText(QRect(area.right() - detailWidth + 1, area.y(), detailWidth, area.height()),
          Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(description, Qt::ElideRight, detailWidth));
        painter->restore();
    }
};

class FileButton : public QToolButton
{
  public:
    using QToolButton::QToolButton;
  protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = QIcon();
        QPainter painter(this);
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
        painter.setPen(palette().color(QPalette::ButtonText));
        icon().paint(&painter, QRect(6, (height() - 16) / 2, 16, 16), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
        const QRect area = rect().adjusted(28, 0, -6, 0);
        painter.drawText(area, Qt::AlignVCenter | Qt::AlignLeft,
          fontMetrics().elidedText(text(), Qt::ElideMiddle, qMax(0, area.width())));
    }
};
}

class InspectorLayerFilter : public QSortFilterProxyModel
{
  public:
    explicit InspectorLayerFilter(QObject* parent) : QSortFilterProxyModel(parent) {}
    QString query;
    QStringList activeKeys;
    bool multipart = false;
    void refilter() { invalidateFilter(); }
    QVariant data(const QModelIndex& index, int role) const override
    {
        const auto source = mapToSource(index);
        const auto* item = layerItem(source);
        if (!item) return {};
        if (role == Qt::DecorationRole) return ViewerIcons::icon(objectIcon(item));
        if (role == ObjectIconRole) return int(objectIcon(item));
        if (role == PreviewRole)
            return isDisplayable(item) && activeKeys.contains(LayerModel::previewKey(item))
              ? (activeKeys.size() > 1 ? 2 : 1) : 0;
        if (role == Qt::AccessibleTextRole)
            return objectKind(item) + QStringLiteral(": ") + source.data().toString();
        if (role == DescriptionRole) {
            QString description;
            if (isChannel(item)) description = pixelType(item->getPixelType());
            else if (isDisplayable(item)) {
                QStringList names;
                std::set<Imf::PixelType> types;
                collectChannels(item, names, types);
                description = formatTypes(types, false);
            }
            return description;
        }
        if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) {
            const QString action = isDisplayable(item) ? tr("Double-click or press Enter to open preview.")
                                                      : tr("Double-click to expand or collapse.");
            QStringList lines;
            lines << objectKind(item) + QStringLiteral(": ") + layerFullName(item);
            if (multipart) {
                lines << tr("Part %1").arg(item->getPart());
                if (item->getType() == LayerItem::PART) lines << partExplanation();
            }
            const QString format = data(index, DescriptionRole).toString();
            if (!format.isEmpty()) lines << format;
            const int preview = data(index, PreviewRole).toInt();
            if (preview) lines << (preview == 2 ? tr("Source of the current stereo preview") : tr("Current preview"));
            lines << action;
            const QString text = lines.join('\n');
            return role == Qt::ToolTipRole ? tooltipText(text) : text;
        }
        return QSortFilterProxyModel::data(index, role);
    }
  protected:
    bool matches(const QModelIndex& index) const
    {
        const auto* item = layerItem(index);
        return item && (QString::fromStdString(item->getOriginalFullName()).contains(query, Qt::CaseInsensitive)
          || QString::fromStdString(item->getFullName()).contains(query, Qt::CaseInsensitive)
          || index.data().toString().contains(query, Qt::CaseInsensitive));
    }
    bool descendantMatches(const QModelIndex& index) const
    {
        if (matches(index)) return true;
        for (int row = 0; row < sourceModel()->rowCount(index); ++row)
            if (descendantMatches(sourceModel()->index(row, 0, index))) return true;
        return false;
    }
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        if (query.isEmpty()) return true;
        for (auto ancestor = parent; ancestor.isValid(); ancestor = ancestor.parent())
            if (matches(ancestor)) return true;
        return descendantMatches(sourceModel()->index(row, 0, parent));
    }
};

class InspectorAttributeFilter : public QSortFilterProxyModel
{
  public:
    explicit InspectorAttributeFilter(QObject* parent) : QSortFilterProxyModel(parent) {}
    QPersistentModelIndex scope;
    QString query;
    void refilter() { invalidateFilter(); }
    QVariant data(const QModelIndex& index, int role) const override
    {
        const auto source = mapToSource(index);
        if (role == Qt::ToolTipRole) {
            const auto name = source.sibling(source.row(), 0).data().toString();
            const auto type = source.sibling(source.row(), 2).data().toString();
            QString text = name + " (" + type + ")\n" + source.sibling(source.row(), 1).data().toString();
            if (type == "part") text += "\n" + partExplanation();
            return tooltipText(text);
        }
        if (role == Qt::DisplayRole && index.column() == 1) {
            // Full text remains available in the read-only value viewer and Copy.
            QString value = source.data().toString();
            return value.replace('\n', QStringLiteral(" ↵ ")).replace('\t', ' ');
        }
        return QSortFilterProxyModel::data(index, role);
    }
  protected:
    bool matches(const QModelIndex& index) const
    {
        return index.data().toString().contains(query, Qt::CaseInsensitive)
          || index.sibling(index.row(), 1).data().toString().contains(query, Qt::CaseInsensitive);
    }
    bool descendantMatches(const QModelIndex& index) const
    {
        if (matches(index)) return true;
        for (int row = 0; row < sourceModel()->rowCount(index); ++row)
            if (descendantMatches(sourceModel()->index(row, 0, index))) return true;
        return false;
    }
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        const auto index = sourceModel()->index(row, 0, parent);
        // Keep the root and its ancestors mapped even when no attributes match.
        for (QModelIndex ancestor = scope; ancestor.isValid(); ancestor = ancestor.parent())
            if (index == ancestor) return true;
        bool inside = false;
        for (auto ancestor = parent; ancestor.isValid(); ancestor = ancestor.parent()) {
            if (ancestor == scope) { inside = true; break; }
        }
        if (!inside) return false;
        if (query.isEmpty()) return true;
        for (auto ancestor = parent; ancestor.isValid() && ancestor != scope; ancestor = ancestor.parent())
            if (matches(ancestor)) return true;
        return descendantMatches(index);
    }
};

InspectorWidget::InspectorWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("inspector");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(280);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(tr("Inspector"), this);
    title->setObjectName("inspectorHeading");
    heading->addWidget(title);
    heading->addStretch();
    auto* close = new QToolButton(this);
    ViewerIcons::setupButton(close, ViewerIcons::Close, tr("Close Inspector"), tr("Close Inspector"));
    close->setObjectName("inspectorClose");
    connect(close, &QToolButton::clicked, this, &InspectorWidget::closeRequested);
    heading->addWidget(close);
    layout->addLayout(heading);

    m_file = new FileButton(this);
    m_file->setObjectName("inspectorFile");
    m_file->setCheckable(true);
    m_file->setIcon(ViewerIcons::icon(ViewerIcons::File));
    m_file->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_file->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_file->setMinimumHeight(28);
    m_file->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_file, &QToolButton::clicked, this, &InspectorWidget::inspectFile);
    connect(m_file, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        if (!m_source || m_source->isStream()) return;
        const QString path = m_source->getFilename();
        QMenu menu(this);
        auto* copy = menu.addAction(ViewerIcons::icon(ViewerIcons::Copy), tr("Copy file path"));
        if (menu.exec(m_file->mapToGlobal(position)) == copy)
            QApplication::clipboard()->setText(path);
    });
    layout->addWidget(m_file);
    m_fileSummary = textLabel({}, this);
    m_fileSummary->setObjectName("inspectorMuted");
    layout->addWidget(m_fileSummary);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(5);
    layout->addWidget(m_splitter, 1);
    auto* navigation = new QWidget(m_splitter);
    navigation->setObjectName("inspectorNavigationBody");
    auto* navLayout = new QVBoxLayout(navigation);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(8);
    m_layerSearch = new QLineEdit(navigation);
    m_layerSearch->setPlaceholderText(tr("Search layers…"));
    m_layerSearch->setAccessibleName(tr("Find layer or channel"));
    m_layerSearch->setToolTip(tr("Find a layer or channel by its full name"));
    auto* layerSearchIcon = m_layerSearch->addAction(ViewerIcons::icon(ViewerIcons::Search), QLineEdit::LeadingPosition);
    layerSearchIcon->setEnabled(false); // Decorative; typing in the field performs the search.
    m_layerSearch->setClearButtonEnabled(true);
    navLayout->addWidget(m_layerSearch);
    m_layers = new InspectorLayerFilter(this);
    m_navigation = new QTreeView(navigation);
    m_navigation->setObjectName("inspectorNavigation");
    m_navigation->setAccessibleName(tr("Parts, layers and channels"));
    m_navigation->setModel(m_layers);
    m_navigation->setItemDelegate(new LayerDelegate(m_navigation));
    m_navigation->setHeaderHidden(true);
    m_navigation->setIndentation(16);
    m_navigation->setUniformRowHeights(true);
    m_navigation->setExpandsOnDoubleClick(false);
    m_navigation->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_navigation->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_navigation->header()->setStretchLastSection(false);
    m_navigation->header()->setSectionResizeMode(QHeaderView::Stretch);
    m_navigation->installEventFilter(this);
    navLayout->addWidget(m_navigation, 1);
    m_noLayers = textLabel(tr("No matching layers or channels."), navigation);
    m_noLayers->hide();
    navLayout->addWidget(m_noLayers);
    connect(m_layerSearch, &QLineEdit::textChanged, this, &InspectorWidget::filterLayers);
    connect(m_navigation, &QTreeView::doubleClicked, this, &InspectorWidget::activate);
    connect(m_navigation->selectionModel(), &QItemSelectionModel::currentChanged,
      this, [this](const QModelIndex& current) {
          if (m_binding || !current.isValid()) return;
          m_inspected = m_layers->mapToSource(current);
          updateDetails();
      });

    m_detailsScroll = new QScrollArea(m_splitter);
    auto* scroll = m_detailsScroll;
    scroll->setObjectName("inspectorDetails");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* details = new QWidget(scroll);
    details->setObjectName("inspectorDetailsBody");
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 12, 0, 0);
    detailsLayout->setSpacing(12);
    m_summary = new QWidget(details);
    detailsLayout->addWidget(m_summary);
    m_partSummary = new QWidget(details);
    m_partButton = foldButton(tr("Image info"), m_partSummary, detailsLayout);
    m_attributesBody = new QWidget(details);
    m_attributesButton = foldButton(tr("All attributes"), m_attributesBody, detailsLayout);
    auto* attributesLayout = new QVBoxLayout(m_attributesBody);
    attributesLayout->setContentsMargins(0, 0, 0, 0);
    attributesLayout->setSpacing(8);
    m_attributeSearch = new QLineEdit(m_attributesBody);
    m_attributeSearch->setPlaceholderText(tr("Search attributes…"));
    m_attributeSearch->setAccessibleName(tr("Find attribute name or value"));
    m_attributeSearch->setToolTip(tr("Find an attribute by name or value"));
    auto* attributeSearchIcon = m_attributeSearch->addAction(ViewerIcons::icon(ViewerIcons::Search), QLineEdit::LeadingPosition);
    attributeSearchIcon->setEnabled(false);
    m_attributeSearch->setClearButtonEnabled(true);
    attributesLayout->addWidget(m_attributeSearch);
    m_attributes = new InspectorAttributeFilter(this);
    m_attributeTree = new QTreeView(m_attributesBody);
    m_attributeTree->setObjectName("inspectorAttributes");
    m_attributeTree->setAccessibleName(tr("Read-only attributes"));
    m_attributeTree->setModel(m_attributes);
    m_attributeTree->setUniformRowHeights(true);
    m_attributeTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_attributeTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_attributeTree->setIndentation(12);
    m_attributeTree->setMinimumHeight(200);
    m_attributeTree->setMaximumHeight(320);
    m_attributeTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_attributeTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_attributeTree->header()->setStretchLastSection(true);
    m_attributeTree->installEventFilter(this);
    attributesLayout->addWidget(m_attributeTree);
    m_noAttributes = textLabel(tr("No matching attributes."), m_attributesBody);
    attributesLayout->addWidget(m_noAttributes);
    m_attributeTitleRow = new QWidget(m_attributesBody);
    auto* attributeTitleLayout = new QHBoxLayout(m_attributeTitleRow);
    attributeTitleLayout->setContentsMargins(0, 0, 0, 0);
    attributeTitleLayout->setSpacing(6);
    m_attributeTitle = textLabel({}, m_attributeTitleRow);
    m_attributeTitle->setObjectName("inspectorMuted");
    attributeTitleLayout->addWidget(m_attributeTitle, 1);
    m_copyAttribute = new QToolButton(m_attributeTitleRow);
    ViewerIcons::setupButton(m_copyAttribute, ViewerIcons::Copy, tr("Copy full attribute value"),
                             tr("Copy full attribute value"));
    attributeTitleLayout->addWidget(m_copyAttribute);
    attributesLayout->addWidget(m_attributeTitleRow);
    m_attributeValue = new QPlainTextEdit(m_attributesBody);
    m_attributeValue->setAccessibleName(tr("Full attribute value"));
    m_attributeValue->setReadOnly(true);
    m_attributeValue->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_attributeValue->setMinimumHeight(100);
    m_attributeValue->setMaximumHeight(160);
    attributesLayout->addWidget(m_attributeValue);
    connect(m_copyAttribute, &QToolButton::clicked, this, [this] {
        const auto source = m_attributes->mapToSource(m_attributeTree->currentIndex());
        if (source.isValid()) QApplication::clipboard()->setText(attributeValue(source));
    });
    connect(m_attributeSearch, &QLineEdit::textChanged, this, &InspectorWidget::filterAttributes);
    connect(m_attributeTree->selectionModel(), &QItemSelectionModel::currentChanged,
      this, [this](const QModelIndex& current) { if (!m_binding) showAttributeValue(current); });
    connect(m_attributeTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const auto index = m_attributeTree->indexAt(position);
        if (!index.isValid()) return;
        m_attributeTree->setCurrentIndex(index);
        const auto mapped = m_attributes->mapToSource(index);
        const auto source = mapped.sibling(mapped.row(), 0);
        // A refresh can complete while the menu's nested event loop is running.
        const QString value = attributeValue(source);
        const QString name = source.data().toString();
        QMenu menu(this);
        auto* copyValue = menu.addAction(ViewerIcons::icon(ViewerIcons::Copy), tr("Copy full value"));
        auto* copyName = menu.addAction(ViewerIcons::icon(ViewerIcons::Copy), tr("Copy name"));
        const auto* choice = menu.exec(m_attributeTree->viewport()->mapToGlobal(position));
        if (choice == copyValue) QApplication::clipboard()->setText(value);
        else if (choice == copyName) QApplication::clipboard()->setText(name);
    });
    detailsLayout->addStretch();
    scroll->setWidget(details);
    m_splitter->addWidget(navigation);
    m_splitter->addWidget(scroll);
    m_splitter->setSizes({240, 360});
    setDocument(nullptr);
}

void InspectorWidget::setDocument(OpenEXRImage* source)
{
    const QScopedValueRollback<bool> binding(m_binding, true);
    m_inspected = QPersistentModelIndex();
    m_attributeRoot = QPersistentModelIndex();
    m_source = source;
    m_layers->activeKeys.clear();
    m_layers->multipart = source && source->parts() > 1;
    m_layers->query.clear();
    m_attributes->query.clear();
    m_attributes->scope = QPersistentModelIndex();
    m_layers->setSourceModel(source ? source->getLayerModel() : nullptr);
    m_attributes->setSourceModel(source ? source->getHeaderModel() : nullptr);
    {
        const QSignalBlocker layers(m_layerSearch), attributes(m_attributeSearch);
        m_layerSearch->clear();
        m_attributeSearch->clear();
    }
    m_searching = false;
    m_unfilteredExpansion.clear();
    m_layerSearch->setEnabled(source != nullptr);
    m_file->setEnabled(source != nullptr);
    m_file->setText(!source ? tr("Reading file…") : source->isStream() ? tr("Stream")
      : QFileInfo(source->getFilename()).fileName());
    m_file->setAccessibleName(tr("Inspect file: %1").arg(m_file->text()));
    m_file->setToolTip(source ? tooltipText((source->isStream() ? tr("Stream") : source->getFilename())
      + "\n" + tr("Click to inspect the file")) : QString());
    m_fileSummary->setText(!source ? QString() : source->parts() == 1
      ? dimensions(source->header(0).displayWindow())
      : tr("%1 parts").arg(source->parts()));
    m_fileSummary->setToolTip(source && source->parts() > 1 ? tooltipText(partExplanation()) : QString());
    if (source) {
        m_navigation->setColumnHidden(LayerModel::TYPE, true);
        m_navigation->setColumnHidden(LayerModel::PIXELTYPE, true);
        m_attributeTree->setColumnHidden(2, true);
        m_attributeTree->setColumnWidth(0, 130);
        // Refresh subsequently restores the user's saved expansion state.
        m_navigation->expandAll();
    }
    m_noLayers->hide();
    updateDetails();
}

void InspectorWidget::inspectFile()
{
    const QScopedValueRollback<bool> binding(m_binding, true);
    m_inspected = QPersistentModelIndex();
    m_navigation->selectionModel()->clear();
    updateDetails();
}

void InspectorWidget::inspectLayer(const QModelIndex& sourceIndex)
{
    if (!sourceIndex.isValid() || !m_source || sourceIndex.model() != m_source->getLayerModel()) return;
    if (!m_layers->mapFromSource(sourceIndex).isValid()) m_layerSearch->clear();
    const auto index = m_layers->mapFromSource(sourceIndex);
    if (!index.isValid()) return;
    const QScopedValueRollback<bool> binding(m_binding, true);
    m_inspected = sourceIndex;
    m_navigation->setCurrentIndex(index);
    for (auto parent = index.parent(); parent.isValid(); parent = parent.parent())
        m_navigation->expand(parent);
    m_navigation->scrollTo(index);
    updateDetails();
}

void InspectorWidget::setActivePreviews(const QStringList& keys, bool followSelection)
{
    m_layers->activeKeys = keys;
    m_navigation->viewport()->update();
    if (!followSelection || !m_source || keys.isEmpty()) return;
    QModelIndex selected;
    visitTree(m_source->getLayerModel(), {}, [&](const QModelIndex& index) {
        if (!selected.isValid() && isDisplayable(layerItem(index))
            && keys.contains(LayerModel::previewKey(layerItem(index)))) selected = index;
    });
    if (selected.isValid()) inspectLayer(selected);
}

void InspectorWidget::updateDetails()
{
    m_detailsScroll->verticalScrollBar()->setValue(0);
    auto* form = newForm(m_summary);
    auto* partForm = newForm(m_partSummary);
    m_file->setChecked(!m_inspected.isValid());
    m_partButton->hide();
    m_partSummary->hide();
    m_attributesButton->setVisible(m_source != nullptr);
    m_attributesBody->setVisible(m_source && m_attributesButton->isChecked());
    if (!m_source) return;

    const auto* item = layerItem(m_inspected);
    const QString kind = objectKind(item);
    const QString name = item ? m_inspected.data().toString() : m_file->text();
    auto* titleRow = new QWidget(m_summary);
    auto* titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(8);
    auto* badge = new ObjectBadge(objectIcon(item), kind, titleRow);
    if (item && item->getType() == LayerItem::PART)
        badge->setToolTip(tooltipText(kind + "\n" + partExplanation()));
    titleLayout->addWidget(badge);
    auto* objectTitle = textLabel(name, titleRow);
    objectTitle->setObjectName("inspectorHeading");
    objectTitle->setAccessibleName(kind + QStringLiteral(": ") + name);
    titleLayout->addWidget(objectTitle, 1);
    form->addRow(titleRow);

    if (!item) {
        addField(form, tr("Format"), m_source->isRadiance() ? tr("Radiance RGBE") : tr("OpenEXR"));
        addPathField(form, m_source);
        if (m_source->parts() > 1) addField(form, tr("Parts"), QString::number(m_source->parts()));
        if (m_source->parts() == 1) addPartFields(form, m_source, 0);
        m_attributeRoot = m_source->getHeaderModel()->index(0, 0);
        m_attributesButton->setText(m_source->parts() > 1 ? tr("All attributes") : tr("Attributes"));
        m_attributesButton->setToolTip(m_source->parts() > 1 ? tr("Attributes of all Parts in the file")
          : tr("Image attributes shared by its layers and channels"));
    } else {
        const int part = item->getPart();
        const auto& header = m_source->header(part);
        if (item->getType() == LayerItem::PART && !isChannel(item)) {
            addField(form, tr("Part ID"), QString::number(part));
            addPartFields(form, m_source, part);
        } else {
            addField(form, tr("Full name"), layerFullName(item));
            if (m_source->parts() > 1)
                addField(form, tr("Part"), header.hasName() && !header.name().empty()
                  ? tr("%1 · %2").arg(part).arg(QString::fromStdString(header.name())) : QString::number(part));
            QStringList channels;
            std::set<Imf::PixelType> types;
            collectChannels(item, channels, types);
            addField(form, tr("Pixel format"), formatTypes(types, true));
            const auto views = ViewMetadata::read(header);
            if (views.present()) {
                QStringList labels;
                for (const auto& channel : channels) {
                    QString view = QString::fromStdString(views.channelView(channel.toStdString()));
                    if (view.isEmpty()) view = tr("No view");
                    if (!labels.contains(view)) labels << view;
                }
                addField(form, tr("View"), labels.join(", "));
            }
            if (isChannel(item)) {
                const auto* channel = header.channels().findChannel(item->getOriginalFullName());
                if (channel) {
                    addField(form, tr("Sampling"), QStringLiteral("%1 × %2").arg(channel->xSampling).arg(channel->ySampling));
                    addField(form, QStringLiteral("pLinear"), channel->pLinear ? tr("True") : tr("False"));
                }
            } else addField(form, tr("Channels"), channels.join(", "));
            addPartFields(partForm, m_source, part);
            m_partButton->setText(m_source->parts() > 1 ? tr("Part %1 info").arg(part) : tr("Image info"));
            m_partButton->setAccessibleName(m_partButton->text());
            m_partButton->setToolTip(m_source->parts() > 1 ? tooltipText(partExplanation())
              : tr("Image dimensions, windows and storage"));
            m_partButton->show();
            m_partSummary->setVisible(m_partButton->isChecked());
        }
        m_attributeRoot = m_source->getHeaderModel()->partIndex(part);
        m_attributesButton->setText(m_source->parts() > 1 ? tr("Part %1 attributes").arg(part) : tr("Attributes"));
        m_attributesButton->setToolTip(m_source->parts() > 1
          ? tr("Attributes of the selected object's Part, shared by its layers and channels")
          : tr("Image attributes shared by its layers and channels"));
    }
    m_attributesButton->setAccessibleName(m_attributesButton->text());
    filterAttributes(m_attributeSearch->text());
}

void InspectorWidget::activate(const QModelIndex& proxyIndex)
{
    const auto source = m_layers->mapToSource(proxyIndex);
    const auto* item = layerItem(source);
    if (isDisplayable(item)) emit layerActivated(source);
    else if (proxyIndex.isValid()) m_navigation->setExpanded(proxyIndex, !m_navigation->isExpanded(proxyIndex));
}

bool InspectorWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_attributeTree && event->type() == QEvent::ShortcutOverride
        && static_cast<QKeyEvent*>(event)->matches(QKeySequence::Copy)) {
        // Reserve Copy for metadata instead of the window's Copy Preview action.
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (watched == m_navigation && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            activate(m_navigation->currentIndex());
            return true;
        }
        if (watched == m_attributeTree && key->matches(QKeySequence::Copy)) {
            const auto source = m_attributes->mapToSource(m_attributeTree->currentIndex());
            QApplication::clipboard()->setText(attributeValue(source));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QStringList InspectorWidget::expandedObjects() const
{
    QStringList result;
    visitTree(m_layers, {}, [&](const QModelIndex& index) {
        if (m_navigation->isExpanded(index)) result << objectKey(m_layers->mapToSource(index));
    });
    return result;
}

void InspectorWidget::restoreExpandedObjects(const QStringList& keys)
{
    m_navigation->collapseAll();
    visitTree(m_layers, {}, [&](const QModelIndex& index) {
        if (keys.contains(objectKey(m_layers->mapToSource(index)))) m_navigation->expand(index);
    });
}

void InspectorWidget::filterLayers(const QString& query)
{
    const QScopedValueRollback<bool> binding(m_binding, true);
    if (!m_searching && !query.isEmpty()) m_unfilteredExpansion = expandedObjects();
    const bool restoreExpansion = m_searching && query.isEmpty();
    m_layers->query = query;
    m_layers->refilter();
    if (!query.isEmpty()) m_navigation->expandAll();
    m_searching = !query.isEmpty();
    const auto visible = m_layers->mapFromSource(m_inspected);
    if (visible.isValid()) m_navigation->setCurrentIndex(visible);
    else m_navigation->selectionModel()->clear();
    // Changing the current index can reveal its ancestors. Restore folding last.
    if (restoreExpansion) restoreExpandedObjects(m_unfilteredExpansion);
    m_noLayers->setVisible(m_source && m_layers->rowCount() == 0);
}

void InspectorWidget::filterAttributes(const QString& query)
{
    const QScopedValueRollback<bool> binding(m_binding, true);
    m_attributes->scope = m_attributeRoot;
    m_attributes->query = query;
    m_attributes->refilter();
    const auto root = m_attributes->mapFromSource(m_attributeRoot);
    m_attributeTree->setRootIndex(root);
    m_attributeTree->selectionModel()->clear();
    m_attributeTree->collapseAll();
    if (!query.isEmpty()) m_attributeTree->expandAll();
    m_noAttributes->setVisible(m_source && m_attributes->rowCount(root) == 0);
    showAttributeValue({});
}

void InspectorWidget::showAttributeValue(const QModelIndex& proxyIndex)
{
    const auto source = m_attributes->mapToSource(proxyIndex);
    m_attributeTitleRow->setVisible(source.isValid());
    m_copyAttribute->setEnabled(source.isValid());
    m_attributeValue->setVisible(source.isValid());
    if (!source.isValid()) { m_attributeValue->clear(); return; }
    m_attributeTitle->setText(source.sibling(source.row(), 0).data().toString()
      + QStringLiteral(" · ") + source.sibling(source.row(), 2).data().toString());
    const auto type = source.sibling(source.row(), 2).data().toString();
    const bool matrix = type == "m33f" || type == "m33d" || type == "m44f" || type == "m44d";
    m_attributeValue->setLineWrapMode(matrix ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
    m_attributeValue->setPlainText(attributeValue(source));
}

InspectorWidget::NavigationState InspectorWidget::navigationState() const
{
    NavigationState state;
    state.selectedObject = objectKey(m_inspected);
    state.query = m_layerSearch->text();
    state.expandedObjects = m_searching ? m_unfilteredExpansion : expandedObjects();
    return state;
}

void InspectorWidget::restoreNavigationState(const NavigationState& state)
{
    if (!m_source) return;
    m_layerSearch->clear();
    QModelIndex selected;
    visitTree(m_source->getLayerModel(), {}, [&](const QModelIndex& index) {
        if (objectKey(index) == state.selectedObject) selected = index;
    });
    if (selected.isValid()) inspectLayer(selected);
    else inspectFile();
    // Inspection reveals ancestors; saved user folding takes precedence on refresh.
    restoreExpandedObjects(state.expandedObjects);
    m_layerSearch->setText(state.query);
}

QByteArray InspectorWidget::splitterState() const { return m_splitter->saveState(); }
void InspectorWidget::restoreSplitterState(const QByteArray& state)
{
    if (!state.isEmpty()) m_splitter->restoreState(state);
}
