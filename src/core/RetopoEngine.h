#pragma once

#include "core/Mesh.h"
#include "core/RetopoSettings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace retoprime {

struct RetopoRequest {
    Mesh source;
    RetopoSettings settings;
    std::filesystem::path workspace;
};

class IRetopoProcess : public QObject {
    Q_OBJECT

public:
    // Implementations execute synchronously in their QObject thread. An injected
    // process must therefore share RetopoEngine's thread for its entire lifetime.
    using QObject::QObject;
    ~IRetopoProcess() override = default;

    virtual void start(quint64 requestId,
                       const std::filesystem::path& executable,
                       const QStringList& arguments,
                       const std::filesystem::path& workingDirectory) = 0;
    virtual void requestTermination(std::chrono::milliseconds forceKillAfter) = 0;
    virtual void stopAndWait(std::chrono::milliseconds gracefulWait,
                             std::chrono::milliseconds killWait) = 0;

signals:
    void finished(quint64 requestId, int exitCode);
    void errorOccurred(quint64 requestId, QString message);
};

class RetopoEngine final : public QObject {
    Q_OBJECT

public:
    explicit RetopoEngine(QObject* parent = nullptr);
    RetopoEngine(std::filesystem::path helperExecutable,
                 std::filesystem::path applicationDirectory,
                 QObject* parent = nullptr);
    RetopoEngine(IRetopoProcess* process,
                 std::filesystem::path helperExecutable,
                 std::filesystem::path applicationDirectory,
                 QObject* parent = nullptr);
    ~RetopoEngine() override;

    RetopoEngine(const RetopoEngine&) = delete;
    RetopoEngine& operator=(const RetopoEngine&) = delete;

    [[nodiscard]] static QStringList argumentsFor(
        const RetopoSettings& settings,
        const QString& inputPath,
        const QString& outputPath);
    [[nodiscard]] static std::filesystem::path helperExecutableName();

    void start(const RetopoRequest& request);
    void cancel();

signals:
    void progressChanged(int value, QString message);
    void completed(retoprime::Mesh mesh);
    void failed(QString message);

private:
    void connectProcessSignals();
    void processFinished(quint64 requestId, int exitCode);
    void processError(quint64 requestId, const QString& message);
    void failActive(const QString& message);
    [[nodiscard]] QString cleanupFiles();
    [[nodiscard]] bool helperIsConfined() const;
    [[nodiscard]] bool writeTriangulatedInput(const Mesh& source, QString& errorMessage) const;

    enum class State {
        Idle,
        Running,
        Cancelling,
    };

    IRetopoProcess* process_ = nullptr;
    std::filesystem::path applicationDirectory_;
    std::filesystem::path helperExecutable_;
    std::filesystem::path workspaceRoot_;
    std::filesystem::path requestDirectory_;
    std::filesystem::path requestDirectoryIdentity_;
    std::optional<std::array<std::uint64_t, 2>> requestDirectoryObjectIdentity_;
    std::filesystem::path inputPath_;
    std::filesystem::path outputPath_;
    quint64 nextRequestId_ = 1;
    quint64 activeRequestId_ = 0;
    State state_ = State::Idle;
    std::chrono::milliseconds terminationGrace_{1500};
    std::chrono::milliseconds shutdownKillWait_{1500};
};

} // namespace retoprime

Q_DECLARE_METATYPE(retoprime::Mesh)
