#include "SaveImageDialog.h"
#include "ui_SaveImageDialog.h"

#include "ComboBoxBehavior.h"

#include <QComboBox>
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
    options.conflict = ImageSave::ConflictAsk;
    options.bracketCount = ui->bracketCountCombo->currentData().toInt();
    options.bracketStepEv = ui->bracketStepSpinBox->value();
    options.bracketCenterEv = ui->bracketCenterSpinBox->value();

    return options;
}


void SaveImageDialog::setStatus(const QString& message, bool error)
{
    m_readinessStatus = false;
    ui->statusLabel->setText(message);
    ui->statusLabel->setStyleSheet(
      error ? "color: rgb(220, 80, 80);" : "color: rgb(80, 170, 80);");
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
        const bool enabled = !derived || value == ImageSave::TargetPreview || value == ImageSave::TargetLayeredOriginal;
        if (items) items->item(i)->setEnabled(enabled);
        ui->targetCombo->setItemData(i, enabled ? QString() : tr("Anaglyph is a derived preview. Select an eye layer for source/HDR export."), Qt::ToolTipRole);
    }
    if (derived && target() != ImageSave::TargetPreview && target() != ImageSave::TargetLayeredOriginal)
        setComboData(ui->targetCombo, ImageSave::TargetPreview);
    updateSaveAvailability();
}


void SaveImageDialog::updateSaveAvailability()
{
    const bool waiting = target() == ImageSave::TargetPreview && !m_previewReady;
    const bool unsupported = m_derived && target() != ImageSave::TargetPreview && target() != ImageSave::TargetLayeredOriginal;
    ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(m_sourceAvailable && !waiting && !unsupported);
    if (!m_sourceAvailable || waiting) {
        const QString message = !m_sourceAvailable ? tr("Image is no longer available.")
                                  : !m_previewError.isEmpty() ? m_previewError
                                  : tr("Updating preview...");
        setStatus(message, !m_sourceAvailable || !m_previewError.isEmpty());
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
      || target() == ImageSave::TargetHdrBracketedImages) {
        ui->formatCombo->addItem(tr("PNG"), ImageSave::FormatPng);
        ui->formatCombo->addItem(tr("JPEG"), ImageSave::FormatJpeg);
    }

    if (target() == ImageSave::TargetActiveOriginal) {
        ui->formatCombo->addItem(tr("EXR"), ImageSave::FormatExr);
        ui->formatCombo->addItem(tr("HDR"), ImageSave::FormatHdr);
    }

    if (target() == ImageSave::TargetLayeredOriginal) {
        ui->formatCombo->addItem(tr("EXR"), ImageSave::FormatExr);
    }

    const int index = ui->formatCombo->findData(currentFormat);
    if (index >= 0) ui->formatCombo->setCurrentIndex(index);

    ui->formatCombo->blockSignals(false);

    const bool preview = target() == ImageSave::TargetPreview;
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
