#pragma once
#include <QDialog>
#include <anvil/modeldownload.h>
class ModelDownloadDialog final : public QDialog {
public:
    explicit ModelDownloadDialog(QWidget *parent=nullptr);
    QString modelPath() const { return path_; }
protected:
    void reject() override;
private:
    anvil::ModelDownload download_;
    QString path_;
};
