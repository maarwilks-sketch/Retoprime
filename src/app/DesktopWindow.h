#pragma once

#include "core/MeshIO.h"
#include "core/RetopoEngine.h"
#include <QMainWindow>
#include <QTemporaryDir>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace retoprime {
class MeshPreview;

class DesktopWindow final : public QMainWindow {
public:
    explicit DesktopWindow(QWidget* parent = nullptr);
    void smokeTest(const QString& input, const QString& output);
private:
    bool loadMesh(const QString& path);
    void runRetopology();
    bool saveMesh(const QString& path);
    void setBusy(bool busy);
    void showError(const QString& message);
    Mesh source_;
    Mesh result_;
    MeshIO io_;
    QTemporaryDir workspace_;
    RetopoEngine engine_;
    MeshPreview* preview_;
    QLabel* details_;
    QLabel* status_;
    QPushButton* open_;
    QPushButton* run_;
    QPushButton* cancel_;
    QPushButton* save_;
    QSpinBox* target_;
    QCheckBox* sharp_;
    QCheckBox* showResult_;
    QProgressBar* progress_;
    QString sourcePath_;
    QString smokeOutput_;
    bool testing_ = false;
};
}
