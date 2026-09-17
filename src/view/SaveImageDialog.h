#pragma once

#include <io/ImageSave.h>

#include <QDialog>

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;

namespace Ui
{
    class SaveImageDialog;
}

class SaveImageDialog: public QDialog
{
    Q_OBJECT

  public:
    explicit SaveImageDialog(const QString& path, QWidget* parent = nullptr);
    ~SaveImageDialog();

    ImageSave::Options options() const;
    void               setStatus(const QString& message, bool error);
    void setResolutionLevelInfo(ResolutionLevel level, bool multilevel);
    void setDeepSourceInfo(bool activeDeep, bool fileDeep);
    void setEnvironmentSource(const EnvironmentProjection::Snapshot& source);
    void setSourceState(bool available, bool previewReady, const QString& error = QString(), bool derived = false);

  public slots:
    void reject() override;

  signals:
    void saveRequested();

  private:
    void setupOptions();
    void restoreLastOptions();
    void rememberOptions();
    void updateOptions();
    void updateSaveAvailability();
    void updatePathExtension();
    void requestSave();
    void browse();
    void updateProjectionControls(bool defaults);

    ImageSave::Target target() const;
    ImageSave::Format format() const;

    Ui::SaveImageDialog* ui;
    bool m_sourceAvailable = false;
    bool m_previewReady = false;
    bool m_derived = false;
    bool m_activeDeep = false;
    bool m_fileDeep = false;
    bool m_readinessStatus = true;
    QString m_previewError;
    EnvironmentProjection::Snapshot m_environment;
    QWidget* m_projectionPanel = nullptr;
    QComboBox* m_projectionType = nullptr;
    QSpinBox* m_projectionWidth = nullptr;
    QSpinBox* m_projectionHeight = nullptr;
    QDoubleSpinBox* m_yaw = nullptr;
    QDoubleSpinBox* m_pitch = nullptr;
    QDoubleSpinBox* m_fov = nullptr;
};
