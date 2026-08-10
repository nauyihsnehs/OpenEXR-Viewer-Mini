#pragma once

#include <io/ImageSave.h>

#include <QDialog>

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

  public slots:
    void reject() override;

  signals:
    void saveRequested();

  private:
    void setupOptions();
    void restoreLastOptions();
    void rememberOptions();
    void updateOptions();
    void updatePathExtension();
    void requestSave();
    void browse();

    ImageSave::Target target() const;
    ImageSave::Format format() const;

    Ui::SaveImageDialog* ui;
};
