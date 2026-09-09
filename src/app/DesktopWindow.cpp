#include "app/DesktopWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <exception>

namespace retoprime {
namespace {
std::filesystem::path nativePath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toStdString());
#endif
}
QString counts(const Mesh& mesh)
{
    return QStringLiteral("%1 vertices | %2 faces")
        .arg(static_cast<qulonglong>(mesh.positions.size()))
        .arg(static_cast<qulonglong>(mesh.faces.size()));
}
}

// Lightweight wire preview. Large inputs are sampled for display only;
// the engine and exporter always receive the complete mesh.
class MeshPreview final : public QWidget {
public:
    explicit MeshPreview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(480, 400);
    }
    void setMesh(const Mesh* mesh)
    {
        mesh_ = mesh;
        center_ = Eigen::Vector3f::Zero();
        radius_ = 1.0f;
        if (mesh_ && !mesh_->positions.empty()) {
            Eigen::Vector3f low = mesh_->positions.front();
            Eigen::Vector3f high = low;
            for (const auto& p : mesh_->positions) {
                low = low.cwiseMin(p);
                high = high.cwiseMax(p);
            }
            center_ = (low + high) * 0.5f;
            radius_ = std::max((high - low).norm() * 0.5f, 0.0001f);
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#141b24"));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor("#8e9dad"));
        if (!mesh_ || mesh_->faces.empty()) {
            painter.drawText(rect(), Qt::AlignCenter, "Open an OBJ or FBX mesh to begin");
            return;
        }
        const auto project = [this](const Eigen::Vector3f& point) {
            const Eigen::Vector3f p = (point - center_) / radius_;
            const float x = std::cos(yaw_) * p.x() + std::sin(yaw_) * p.z();
            const float z = -std::sin(yaw_) * p.x() + std::cos(yaw_) * p.z();
            const float y = std::cos(pitch_) * p.y() - std::sin(pitch_) * z;
            const double scale = std::min(width(), height()) * 0.4 * zoom_;
            return QPointF(width() * 0.5 + x * scale, height() * 0.5 - y * scale);
        };
        painter.setPen(QPen(QColor("#62c4cc"), 0.7));
        const std::size_t stride = std::max<std::size_t>(1, (mesh_->faces.size() + 19999) / 20000);
        for (std::size_t i = 0; i < mesh_->faces.size(); i += stride) {
            QPolygonF polygon;
            for (const auto index : mesh_->faces[i]) {
                if (index < mesh_->positions.size())
                    polygon.append(project(mesh_->positions[index]));
            }
            painter.drawPolygon(polygon);
        }
        painter.setPen(QColor("#a7b6c5"));
        painter.drawText(16, 26, stride > 1
            ? "Sampled wire preview — export uses full mesh"
            : "Wire preview");
        painter.drawText(16, height() - 18, "Drag to orbit  |  Scroll to zoom");
    }
    void mousePressEvent(QMouseEvent* event) override { last_ = event->position(); }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            const auto delta = event->position() - last_;
            yaw_ += static_cast<float>(delta.x()) * 0.01f;
            pitch_ += static_cast<float>(delta.y()) * 0.01f;
            last_ = event->position();
            update();
        }
    }
    void wheelEvent(QWheelEvent* event) override
    {
        zoom_ = std::clamp(zoom_ * std::exp(event->angleDelta().y() * 0.001), 0.1, 10.0);
        update();
    }
private:
    const Mesh* mesh_ = nullptr;
    Eigen::Vector3f center_ = Eigen::Vector3f::Zero();
    float radius_ = 1.0f;
    float yaw_ = 0.55f;
    float pitch_ = -0.25f;
    double zoom_ = 1.0;
    QPointF last_;
};

DesktopWindow::DesktopWindow(QWidget* parent)
    : QMainWindow(parent), engine_(nullptr)
{
    setWindowTitle("RETOPRIME — Standalone Retopology");
    resize(1200, 760);
    auto* root = new QWidget(this);
    setCentralWidget(root);
    auto* layout = new QHBoxLayout(root);
    auto* controls = new QVBoxLayout;
    auto* heading = new QLabel("RETOPRIME");
    heading->setStyleSheet("font-size: 26px; font-weight: bold; color: #62c4cc;");
    controls->addWidget(heading);
    auto* subtitle = new QLabel("Import. Generate quads. Export.");
    controls->addWidget(subtitle);
    open_ = new QPushButton("Open OBJ / FBX");
    open_->setObjectName("openMesh");
    controls->addWidget(open_);
    details_ = new QLabel("No mesh loaded");
    details_->setWordWrap(true);
    controls->addWidget(details_);
    controls->addSpacing(20);
    controls->addWidget(new QLabel("Target face count (approximate)"));
    target_ = new QSpinBox;
    target_->setRange(500, 2000000);
    target_->setValue(5000);
    target_->setSingleStep(500);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(500, 100000);
    slider->setValue(5000);
    connect(slider, &QSlider::valueChanged, target_, &QSpinBox::setValue);
    // Above the slider range the numeric field retains its exact value.
    connect(target_, &QSpinBox::valueChanged, this, [slider](int value) {
        const bool previous = slider->blockSignals(true);
        slider->setValue(std::min(value, slider->maximum()));
        slider->blockSignals(previous);
    });
    controls->addWidget(target_);
    controls->addWidget(slider);
    sharp_ = new QCheckBox("Preserve sharp features");
    sharp_->setChecked(true);
    controls->addWidget(sharp_);
    run_ = new QPushButton("Generate quad mesh");
    run_->setObjectName("generateMesh");
    cancel_ = new QPushButton("Cancel");
    save_ = new QPushButton("Save result as OBJ");
    save_->setObjectName("saveMesh");
    controls->addWidget(run_);
    controls->addWidget(cancel_);
    controls->addWidget(save_);
    showResult_ = new QCheckBox("Preview result");
    controls->addWidget(showResult_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    controls->addWidget(progress_);
    status_ = new QLabel("Ready");
    status_->setWordWrap(true);
    controls->addWidget(status_);
    controls->addStretch();
    auto* limitations = new QLabel("Geometry only: UVs and textures are not preserved.\n"
                                  "Face count is a target, not an exact guarantee.");
    limitations->setWordWrap(true);
    controls->addWidget(limitations);
    auto* panel = new QWidget;
    panel->setLayout(controls);
    panel->setFixedWidth(290);
    layout->addWidget(panel);
    preview_ = new MeshPreview;
    layout->addWidget(preview_, 1);
    setStyleSheet("QWidget { background: #222c38; color: #e4eaf1; }"
                  "QPushButton { padding: 10px; background: #36495b; border-radius: 4px; }"
                  "QPushButton:disabled { color: #75808b; }"
                  "QSpinBox { padding: 7px; }");
    setBusy(false);
    connect(open_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Open mesh", {}, "Meshes (*.obj *.fbx)");
        if (!path.isEmpty()) loadMesh(path);
    });
    connect(run_, &QPushButton::clicked, this, &DesktopWindow::runRetopology);
    connect(cancel_, &QPushButton::clicked, &engine_, &RetopoEngine::cancel);
    connect(save_, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, "Save quad mesh",
            QFileInfo(sourcePath_).absolutePath() + "/" + QFileInfo(sourcePath_).completeBaseName()
                + "_RETOPO.obj", "OBJ mesh (*.obj)");
        if (!path.isEmpty()) {
            if (!path.endsWith(".obj", Qt::CaseInsensitive)) path += ".obj";
            saveMesh(path);
        }
    });
    connect(showResult_, &QCheckBox::toggled, this, [this](bool result) {
        preview_->setMesh(result && !result_.faces.empty() ? &result_ : &source_);
    });
    connect(&engine_, &RetopoEngine::progressChanged, this, [this](int value, const QString& message) {
        progress_->setValue(value);
        status_->setText(message);
    });
    connect(&engine_, &RetopoEngine::failed, this, [this](const QString& message) {
        setBusy(false);
        showError(message);
    });
    connect(&engine_, &RetopoEngine::completed, this, [this](Mesh mesh) {
        result_ = std::move(mesh);
        setBusy(false);
        showResult_->setChecked(true);
        preview_->setMesh(&result_);
        details_->setText("Source: " + counts(source_) + "\nResult: " + counts(result_));
        status_->setText("Complete. Save your result as OBJ.");
        if (testing_ && saveMesh(smokeOutput_)) QCoreApplication::exit(0);
    });
}

void DesktopWindow::setBusy(bool busy)
{
    open_->setEnabled(!busy);
    run_->setEnabled(!busy && !source_.faces.empty());
    cancel_->setEnabled(busy);
    save_->setEnabled(!busy && !result_.faces.empty());
    target_->setEnabled(!busy);
    sharp_->setEnabled(!busy);
    showResult_->setEnabled(!result_.faces.empty());
}

void DesktopWindow::showError(const QString& message)
{
    status_->setText(message);
    if (testing_) {
        qCritical().noquote() << message;
        QCoreApplication::exit(1);
    } else {
        QMessageBox::warning(this, "RETOPRIME", message);
    }
}

bool DesktopWindow::loadMesh(const QString& path)
{
    try {
        auto loaded = io_.load(nativePath(path));
        const auto validation = loaded.mesh.validate();
        if (!validation.errors.empty()) {
            showError(validation.errors.join("\n"));
            return false;
        }
        source_ = std::move(loaded.mesh);
        sourcePath_ = path;
        result_ = {};
        showResult_->setChecked(false);
        preview_->setMesh(&source_);
        details_->setText(QFileInfo(path).fileName() + "\n" + counts(source_));
        status_->setText(loaded.warnings.empty() ? "Mesh loaded. Choose a face count."
                                               : loaded.warnings.join("\n"));
        progress_->setValue(0);
        setBusy(false);
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

void DesktopWindow::runRetopology()
{
    if (source_.faces.empty()) return;
    if (!workspace_.isValid()) {
        showError("Unable to create a temporary workspace.");
        return;
    }
    RetopoSettings settings;
    settings.targetFaces = target_->value();
    settings.preserveFeatures = sharp_->isChecked() ? 1.0f : 0.0f;
    setBusy(true);
    progress_->setValue(0);
    engine_.start({source_, settings, nativePath(workspace_.path())});
}

bool DesktopWindow::saveMesh(const QString& path)
{
    try {
        if (QFileInfo(path).absoluteFilePath() == QFileInfo(sourcePath_).absoluteFilePath()) {
            showError("Choose a different output name to preserve your original mesh.");
            return false;
        }
        io_.save(result_, nativePath(path));
        status_->setText("Saved: " + path);
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

void DesktopWindow::smokeTest(const QString& input, const QString& output)
{
    testing_ = true;
    smokeOutput_ = output;
    QTimer::singleShot(0, this, [this, input] {
        if (loadMesh(input)) {
            target_->setValue(500);
            runRetopology();
        }
    });
    QTimer::singleShot(150000, this, [this] {
        engine_.cancel();
        showError("Desktop smoke test timed out.");
    });
}
}
