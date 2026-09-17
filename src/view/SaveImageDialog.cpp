#include "SaveImageDialog.h"
#include "ui_SaveImageDialog.h"

#include "ComboBoxBehavior.h"

#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <algorithm>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QStandardItemModel>

namespace
{
    bool s_hasLastOptions = false;
    ImageSave::Options s_lastOptions;


    void setComboData(QComboBox* combo, int data)
    {
        const int index = combo->findData(data);
        if (index >= 0) combo->setCurrentIndex(index);
    }
}


SaveImageDialog::SaveImageDialog(const QString& path, QWidget* parent)
  : QDialog(parent)
  , ui(new Ui::SaveImageDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("Save Image"));
    setFixedWidth(520);

    ui->resolutionLevelLabel->hide();
    setupOptions();
    applyComboBoxBehavior(this);
    ui->pathEdit->setText(path);
    restoreLastOptions();
    updateOptions();
    updatePathExtension();

    connect(
      ui->targetCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this,
      [this](int) {
          if (target() == ImageSave::TargetProjectionConversion) {
              setComboData(ui->pixelTypeCombo, ImageSave::PixelFloat);
              setComboData(ui->compressionCombo, ImageSave::CompressionZip);
          }
          updateOptions();
          updatePathExtension();
      });
    connect(
      ui->formatCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this,
      [this](int) {
          updateOptions();
          updatePathExtension();
      });
    connect(ui->browseButton, &QPushButton::clicked, this, &SaveImageDialog::browse);
    connect(
      ui->buttonBox,
      &QDialogButtonBox::accepted,
      this,
      &SaveImageDialog::requestSave);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SaveImageDialog::reject);
}


SaveImageDialog::~SaveImageDialog()
{
    delete ui;
}


ImageSave::Options SaveImageDialog::options() const
{
    ImageSave::Options options;
    options.target = target();
    options.format = format();
    options.path = ui->pathEdit->text().trimmed();
    options.maxWidth = ui->sizeCombo->currentData().toInt();
    options.quality = ui->qualitySpinBox->value();
    options.jpegBackground = static_cast<ImageSave::JpegBackground>(
      ui->jpegBackgroundCombo->currentData().toInt());
    options.compression =
      static_cast<ImageSave::ExrCompression>(ui->compressionCombo->currentData().toInt());
    options.pixelType =
      static_cast<ImageSave::ExrPixelType>(ui->pixelTypeCombo->currentData().toInt());
    options.channelScope =
      static_cast<ImageSave::ChannelScope>(ui->channelScopeCombo->currentData().toInt());
    options.metadata =
      static_cast<ImageSave::MetadataPolicy>(ui->metadataCombo->currentData().toInt());
    options.multipart =
      static_cast<ImageSave::MultipartPolicy>(ui->multipartCombo->currentData().toInt());
    if (m_fileDeep && options.target == ImageSave::TargetLayeredOriginal)
        options.multipart = ImageSave::MultipartPreserve;
    options.conflict = ImageSave::ConflictAsk;
    options.bracketCount = ui->bracketCountCombo->currentData().toInt();
    options.bracketStepEv = ui->bracketStepSpinBox->value();
    options.bracketCenterEv = ui->bracketCenterSpinBox->value();

    options.projection.type = EnvironmentProjection::Type(m_projectionType->currentData().toInt());
    options.projection.yaw = m_yaw->value(); options.projection.pitch = m_pitch->value();
    options.projection.fieldOfView = m_fov->value();
    options.projectionSize = QSize(m_projectionWidth->value(), m_projectionHeight->value());
    return options;
}


void SaveImageDialog::setStatus(const QString& message, bool error)
{
    m_readinessStatus = false;
    ui->statusLabel->setText(message);
    ui->statusLabel->setStyleSheet(
      error ? "color: rgb(220, 80, 80);" : "color: rgb(80, 170, 80);");
}


void SaveImageDialog::setResolutionLevelInfo(ResolutionLevel level, bool multilevel)
{
    ui->resolutionLevelLabel->setVisible(multilevel);
    ui->resolutionLevelLabel->setText(tr("Level %1 — selected level only; scanline EXR").arg(QString::fromStdString(level.toString())));
}


void SaveImageDialog::setEnvironmentSource(const EnvironmentProjection::Snapshot& source)
{
    m_environment = source;
    if (source.source && EnvironmentProjection::describe(*source.source).available()) {
        setComboData(m_projectionType, int(source.state.type));
        m_yaw->setValue(source.state.yaw); m_pitch->setValue(source.state.pitch);
        m_fov->setValue(source.state.fieldOfView);
        updateProjectionControls(true);
        if (target() == ImageSave::TargetProjectionConversion) {
            setComboData(ui->pixelTypeCombo, ImageSave::PixelFloat);
            setComboData(ui->compressionCombo, ImageSave::CompressionZip);
        }
    } else if (target() == ImageSave::TargetProjectionConversion)
        setComboData(ui->targetCombo, ImageSave::TargetPreview);
    setSourceState(m_sourceAvailable, m_previewReady, m_previewError, m_derived);
}

void SaveImageDialog::updateProjectionControls(bool defaults)
{
    using namespace EnvironmentProjection;
    const auto type = Type(m_projectionType->currentData().toInt());
    const QSignalBlocker widthBlock(m_projectionWidth), heightBlock(m_projectionHeight);
    if (defaults && m_environment.source) {
        const auto size = defaultSize(*m_environment.source, type);
        m_projectionWidth->setValue(size.width()); m_projectionHeight->setValue(size.height());
    }
    int width = m_projectionWidth->value();
    if (type == LatLong) {
        width = std::max(2, width - width % 2);
        m_projectionWidth->setValue(width); m_projectionHeight->setValue(width / 2);
    } else if (type == Cube) {
        width = std::min(width, m_projectionHeight->maximum() / 6);
        m_projectionWidth->setValue(width); m_projectionHeight->setValue(width * 6);
    } else if (type == Sphere) m_projectionHeight->setValue(width);
    m_projectionWidth->setSingleStep(type == LatLong ? 2 : 1);
    m_projectionHeight->setEnabled(type == Perspective);
    const bool camera = type == Perspective || type == Sphere;
    for (auto* control : {m_yaw, m_pitch, m_fov}) {
        const bool visible = control == m_fov ? type == Perspective : camera;
        control->setVisible(visible);
        static_cast<QFormLayout*>(m_projectionPanel->layout())->labelForField(control)->setVisible(visible);
    }
}

void SaveImageDialog::setDeepSourceInfo(bool activeDeep, bool fileDeep)
{
    m_activeDeep = activeDeep;
    m_fileDeep = fileDeep;
    updateOptions();
}

void SaveImageDialog::setSourceState(bool available, bool previewReady, const QString& error, bool derived)
{
    m_sourceAvailable = available;
    m_previewReady = previewReady;
    m_previewError = error;
    m_derived = derived;
    auto* items = qobject_cast<QStandardItemModel*>(ui->targetCombo->model());
    for (int i = 0; i < ui->targetCombo->count(); ++i) {
        const int value = ui->targetCombo->itemData(i).toInt();
        const bool enabled = (!derived || value == ImageSave::TargetPreview || value == ImageSave::TargetLayeredOriginal)
          && (value != ImageSave::TargetProjectionConversion || (m_environment.source
              && EnvironmentProjection::describe(*m_environment.source).available() && bool(m_environment.mapColors)));
        if (items) items->item(i)->setEnabled(enabled);
        const QString reason = value == ImageSave::TargetProjectionConversion
          ? tr("Requires a completed preview from a complete LatLong or Cube source at this level.")
          : tr("Anaglyph is a derived preview. Select an eye layer for source/HDR export.");
        ui->targetCombo->setItemData(i, enabled ? QString() : reason, Qt::ToolTipRole);
    }
    if (derived && target() != ImageSave::TargetPreview && target() != ImageSave::TargetLayeredOriginal)
        setComboData(ui->targetCombo, ImageSave::TargetPreview);
    updateSaveAvailability();
}


void SaveImageDialog::updateSaveAvailability()
{
    const bool conversion = target() == ImageSave::TargetProjectionConversion;
    const bool waiting = target() == ImageSave::TargetPreview && !m_previewReady;
    const bool environmentMissing = conversion && (!m_environment.source || !m_environment.mapColors
      || !EnvironmentProjection::describe(*m_environment.source).available());
    const bool unsupported = m_derived && target() != ImageSave::TargetPreview && target() != ImageSave::TargetLayeredOriginal;
    ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(m_sourceAvailable && !waiting && !unsupported && !environmentMissing);
    if (!m_sourceAvailable || waiting) {
        const QString message = !m_sourceAvailable ? tr("Image is no longer available.")
                                  : !m_previewError.isEmpty() ? m_previewError
                                  : tr("Updating preview...");
        setStatus(message, !m_sourceAvailable || !m_previewError.isEmpty());
        m_readinessStatus = true;
    } else if (environmentMissing) {
        setStatus(tr("Projection conversion requires a completed, complete environment source at this level."), true);
        m_readinessStatus = true;
    } else if (m_readinessStatus) {
        setStatus(tr("Ready"), false);
    }
}


void SaveImageDialog::reject()
{
    rememberOptions();
    QDialog::reject();
}


void SaveImageDialog::setupOptions()
{
    ui->targetCombo->addItem(tr("Preview Image"), ImageSave::TargetPreview);
    ui->targetCombo->addItem(
      tr("Active Original"),
      ImageSave::TargetActiveOriginal);
    ui->targetCombo->addItem(
      tr("Layered Original"),
      ImageSave::TargetLayeredOriginal);
    ui->targetCombo->addItem(
      tr("HDR Bracketed Images"),
      ImageSave::TargetHdrBracketedImages);

    ui->targetCombo->addItem(tr("Projection Conversion"), ImageSave::TargetProjectionConversion);
    m_projectionPanel = new QWidget(this);
    m_projectionPanel->setObjectName("projectionPanel");
    auto* form = new QFormLayout(m_projectionPanel);
    form->setContentsMargins(0, 0, 0, 0);
    m_projectionType = new QComboBox(m_projectionPanel);
    m_projectionType->setObjectName("projectionTypeCombo");
    for (int i = EnvironmentProjection::LatLong; i <= EnvironmentProjection::Sphere; ++i)
        m_projectionType->addItem(EnvironmentProjection::name(EnvironmentProjection::Type(i)), i);
    form->addRow(tr("Output projection"), m_projectionType);
    m_projectionWidth = new QSpinBox(m_projectionPanel);
    m_projectionHeight = new QSpinBox(m_projectionPanel);
    m_projectionWidth->setObjectName("projectionWidth");
    m_projectionHeight->setObjectName("projectionHeight");
    for (auto* spin : {m_projectionWidth, m_projectionHeight}) spin->setRange(1, 1000000);
    form->addRow(tr("Width (pixels)"), m_projectionWidth);
    form->addRow(tr("Height (pixels)"), m_projectionHeight);
    m_projectionWidth->setToolTip(tr("Cube: width is the face edge; the strip is six faces high."));
    m_yaw = new QDoubleSpinBox(m_projectionPanel);
    m_pitch = new QDoubleSpinBox(m_projectionPanel);
    m_fov = new QDoubleSpinBox(m_projectionPanel);
    m_yaw->setObjectName("projectionYaw"); m_pitch->setObjectName("projectionPitch");
    m_fov->setObjectName("projectionFov");
    m_yaw->setRange(-180., 180.); m_pitch->setRange(-90., 90.);
    m_fov->setRange(10., 150.); m_fov->setValue(90.);
    for (auto* spin : {m_yaw, m_pitch, m_fov}) { spin->setDecimals(2); spin->setSuffix(QString::fromUtf8("°")); }
    form->addRow(tr("Yaw (+Z = 0)"), m_yaw);
    form->addRow(tr("Pitch (+Y up)"), m_pitch);
    form->addRow(tr("Horizontal field of view"), m_fov);
    auto* note = new QLabel(tr("EXR: linear values, one scanline part, selected level only.\nPNG/JPEG: current display parameters."), m_projectionPanel);
    note->setWordWrap(true); form->addRow(note);
    ui->verticalLayout->insertWidget(1, m_projectionPanel);
    connect(m_projectionType, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, [this] { updateProjectionControls(true); });
    connect(m_projectionWidth, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
      this, [this] { updateProjectionControls(false); });

    ui->sizeCombo->addItem(tr("Full"), 0);
    ui->sizeCombo->addItem(tr("2048w"), 2048);
    ui->sizeCombo->addItem(tr("1024w"), 1024);
    ui->sizeCombo->addItem(tr("512w"), 512);

    ui->qualitySpinBox->setRange(1, 100);
    ui->qualitySpinBox->setValue(90);
    ui->jpegBackgroundCombo->addItem(tr("Black (0)"), ImageSave::BackgroundBlack);
    ui->jpegBackgroundCombo->addItem(tr("White (1)"), ImageSave::BackgroundWhite);

    ui->bracketCountCombo->addItem(tr("3"), 3);
    ui->bracketCountCombo->addItem(tr("5"), 5);
    ui->bracketCountCombo->addItem(tr("7"), 7);
    ui->bracketCountCombo->addItem(tr("9"), 9);
    ui->bracketCountCombo->setCurrentIndex(1);

    ui->bracketStepSpinBox->setRange(0.5, 4.);
    ui->bracketStepSpinBox->setSingleStep(0.5);
    ui->bracketStepSpinBox->setDecimals(1);
    ui->bracketStepSpinBox->setValue(2.);
    ui->bracketStepSpinBox->setFixedWidth(84);

    ui->bracketCenterSpinBox->setRange(-20., 20.);
    ui->bracketCenterSpinBox->setSingleStep(0.5);
    ui->bracketCenterSpinBox->setDecimals(1);
    ui->bracketCenterSpinBox->setValue(0.);
    ui->bracketCenterSpinBox->setFixedWidth(84);

    ui->compressionCombo->addItem(tr("ZIP"), ImageSave::CompressionZip);
    ui->compressionCombo->addItem(tr("None"), ImageSave::CompressionNone);
    ui->compressionCombo->addItem(tr("RLE"), ImageSave::CompressionRle);
    ui->compressionCombo->addItem(tr("PIZ"), ImageSave::CompressionPiz);
    ui->compressionCombo->addItem(tr("DWAA"), ImageSave::CompressionDwaa);
    ui->compressionCombo->addItem(tr("DWAB"), ImageSave::CompressionDwab);

    ui->pixelTypeCombo->addItem(tr("Float"), ImageSave::PixelFloat);
    ui->pixelTypeCombo->addItem(tr("Half"), ImageSave::PixelHalf);

    ui->channelScopeCombo->addItem(tr("All channels"), ImageSave::ChannelsAll);
    ui->channelScopeCombo->addItem(tr("Color channels (RGB/YC)"), ImageSave::ChannelsRgb);

    ui->metadataCombo->addItem(tr("Basic source-safe"), ImageSave::MetadataBasic);
    ui->metadataCombo->addItem(tr("None"), ImageSave::MetadataNone);

    ui->multipartCombo->addItem(tr("Preserve multipart"), ImageSave::MultipartPreserve);
    ui->multipartCombo->addItem(tr("Flatten to single part"), ImageSave::MultipartFlatten);

    QPushButton* saveButton = ui->buttonBox->button(QDialogButtonBox::Save);
    if (saveButton) saveButton->setText(tr("Save"));
}


void SaveImageDialog::restoreLastOptions()
{
    if (!s_hasLastOptions) return;

    setComboData(ui->targetCombo, s_lastOptions.target);
    updateOptions();
    setComboData(ui->formatCombo, s_lastOptions.format);

    setComboData(ui->sizeCombo, s_lastOptions.maxWidth);
    ui->qualitySpinBox->setValue(s_lastOptions.quality);
    setComboData(ui->jpegBackgroundCombo, s_lastOptions.jpegBackground);

    setComboData(ui->compressionCombo, s_lastOptions.compression);
    setComboData(ui->pixelTypeCombo, s_lastOptions.pixelType);
    setComboData(ui->channelScopeCombo, s_lastOptions.channelScope);
    setComboData(ui->metadataCombo, s_lastOptions.metadata);
    setComboData(ui->multipartCombo, s_lastOptions.multipart);

    setComboData(ui->bracketCountCombo, s_lastOptions.bracketCount);
    ui->bracketStepSpinBox->setValue(s_lastOptions.bracketStepEv);
    ui->bracketCenterSpinBox->setValue(s_lastOptions.bracketCenterEv);
}


void SaveImageDialog::rememberOptions()
{
    s_lastOptions = options();
    s_lastOptions.path.clear();
    s_lastOptions.conflict = ImageSave::ConflictAsk;
    s_hasLastOptions = true;
}


void SaveImageDialog::updateOptions()
{
    const ImageSave::Format currentFormat = format();

    ui->formatCombo->blockSignals(true);
    ui->formatCombo->clear();

    if (
      target() == ImageSave::TargetPreview
      || target() == ImageSave::TargetHdrBracketedImages || target() == ImageSave::TargetProjectionConversion) {
        ui->formatCombo->addItem(tr("PNG"), ImageSave::FormatPng);
        ui->formatCombo->addItem(tr("JPEG"), ImageSave::FormatJpeg);
    }

    if (target() == ImageSave::TargetActiveOriginal) {
        ui->formatCombo->addItem(tr("EXR"), ImageSave::FormatExr);
        ui->formatCombo->addItem(tr("HDR"), ImageSave::FormatHdr);
    }

    if (target() == ImageSave::TargetLayeredOriginal || target() == ImageSave::TargetProjectionConversion) {
        ui->formatCombo->addItem(tr("EXR"), ImageSave::FormatExr);
    }

    const int index = ui->formatCombo->findData(currentFormat);
    if (index >= 0) ui->formatCombo->setCurrentIndex(index);

    ui->formatCombo->blockSignals(false);

    const bool conversion = target() == ImageSave::TargetProjectionConversion;
    const bool preview = target() == ImageSave::TargetPreview || (conversion && format() != ImageSave::FormatExr);
    m_projectionPanel->setVisible(conversion);
    ui->sizeLabel->setVisible(!conversion);
    ui->sizeCombo->setVisible(!conversion);
    ui->channelScopeLabel->setVisible(!conversion); ui->channelScopeCombo->setVisible(!conversion);
    ui->metadataLabel->setVisible(!conversion); ui->metadataCombo->setVisible(!conversion);
    const bool bracket = target() == ImageSave::TargetHdrBracketedImages;
    const bool jpeg = format() == ImageSave::FormatJpeg;
    const bool exr = format() == ImageSave::FormatExr;
    const bool hdr = format() == ImageSave::FormatHdr;
    const bool layered = target() == ImageSave::TargetLayeredOriginal;

    ui->parameterStack->setCurrentWidget(
      preview || bracket ? ui->previewPage : exr ? ui->exrPage : ui->hdrPage);
    ui->qualityLabel->setEnabled(jpeg);
    ui->qualitySpinBox->setEnabled(jpeg);
    ui->jpegBackgroundLabel->setVisible(preview && jpeg);
    ui->jpegBackgroundCombo->setVisible(preview && jpeg);
    ui->bracketCountLabel->setVisible(bracket);
    ui->bracketCountCombo->setVisible(bracket);
    ui->bracketStepLabel->setVisible(bracket);
    ui->bracketStepSpinBox->setVisible(bracket);
    ui->bracketCenterLabel->setVisible(bracket);
    ui->bracketCenterSpinBox->setVisible(bracket);
    ui->multipartCombo->setEnabled(layered);
    ui->multipartLabel->setVisible(layered);
    ui->multipartCombo->setVisible(layered);
    const bool deep = exr && !conversion && (layered ? m_fileDeep : m_activeDeep);
    ui->deepInfoLabel->setVisible(deep);
    ui->compressionCombo->setVisible(!deep);
    ui->pixelTypeCombo->setVisible(!deep);
    ui->compressionLabel->setVisible(!deep);
    ui->pixelTypeLabel->setVisible(!deep);
    ui->deepMultipartLabel->setVisible(layered && deep);
    ui->multipartCombo->setVisible(layered && !deep);
    ui->hdrInfoLabel->setVisible(hdr);
    updateSaveAvailability();
}


void SaveImageDialog::updatePathExtension()
{
    const QString currentPath = ui->pathEdit->text().trimmed();
    if (currentPath.isEmpty()) return;

    QFileInfo info(currentPath);
    const QString path =
      info.path()
      + "/"
      + info.completeBaseName()
      + "."
      + ImageSave::extension(format());

    ui->pathEdit->setText(path);
}


void SaveImageDialog::requestSave()
{
    updateSaveAvailability();
    if (!ui->buttonBox->button(QDialogButtonBox::Save)->isEnabled()) return;
    if (!m_sourceAvailable || (target() == ImageSave::TargetPreview && !m_previewReady))
        return;
    if (m_derived && target() != ImageSave::TargetPreview && target() != ImageSave::TargetLayeredOriginal) return;
    updatePathExtension();

    if (ui->pathEdit->text().trimmed().isEmpty()) {
        setStatus(tr("Choose a save path."), true);
        return;
    }

    rememberOptions();
    emit saveRequested();
}


void SaveImageDialog::browse()
{
    const QString filename = QFileDialog::getSaveFileName(
      this,
      tr("Save Image"),
      ui->pathEdit->text().trimmed(),
      ImageSave::filter(format()));

    if (filename.isEmpty()) return;

    ui->pathEdit->setText(filename);
    updatePathExtension();
}


ImageSave::Target SaveImageDialog::target() const
{
    return static_cast<ImageSave::Target>(
      ui->targetCombo->currentData().toInt());
}


ImageSave::Format SaveImageDialog::format() const
{
    return static_cast<ImageSave::Format>(
      ui->formatCombo->currentData().toInt());
}
