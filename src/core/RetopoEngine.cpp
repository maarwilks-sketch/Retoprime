#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "core/RetopoEngine.h"

#include "core/MeshIO.h"

#include <Eigen/Geometry>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QUuid>

#ifdef _WIN32
#include <AccCtrl.h>
#include <Aclapi.h>
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace retoprime {

namespace {

[[maybe_unused]] QString pathToQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path pathFromQString(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

#if QT_CONFIG(process)

class QProcessAdapter final : public IRetopoProcess {
public:
    using IRetopoProcess::IRetopoProcess;

    void start(quint64 requestId,
               const std::filesystem::path& executable,
               const QStringList& arguments,
               const std::filesystem::path& workingDirectory) override
    {
        auto* process = new QProcess(this);
        activeProcess_ = process;
        QObject::connect(
            process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, process, requestId](int exitCode, QProcess::ExitStatus) {
                if (activeProcess_ == process) {
                    activeProcess_.clear();
                }
                emit finished(requestId, exitCode);
                process->deleteLater();
            });
        QObject::connect(
            process,
            &QProcess::errorOccurred,
            this,
            [this, process, requestId](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) {
                    return;
                }
                if (activeProcess_ == process) {
                    activeProcess_.clear();
                }
                emit errorOccurred(requestId, process->errorString());
                emit finished(requestId, -1);
                process->deleteLater();
            });

        process->setWorkingDirectory(pathToQString(workingDirectory));
        process->setProgram(pathToQString(executable));
        process->setArguments(arguments);
        process->start();
    }

    void requestTermination(std::chrono::milliseconds forceKillAfter) override
    {
        if (activeProcess_.isNull()) {
            return;
        }
        QPointer<QProcess> process = activeProcess_;
        process->terminate();
        QTimer::singleShot(forceKillAfter, process, [process] {
            if (!process.isNull() && process->state() != QProcess::NotRunning) {
                process->kill();
            }
        });
    }

    void stopAndWait(std::chrono::milliseconds gracefulWait,
                     std::chrono::milliseconds killWait) override
    {
        if (activeProcess_.isNull()) {
            return;
        }
        auto* process = activeProcess_.data();
        process->blockSignals(true);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(static_cast<int>(gracefulWait.count()))) {
                process->kill();
                process->waitForFinished(static_cast<int>(killWait.count()));
            }
        }
        activeProcess_.clear();
        delete process;
    }

private:
    QPointer<QProcess> activeProcess_;
};

#else

class QProcessAdapter final : public IRetopoProcess {
public:
    using IRetopoProcess::IRetopoProcess;

    void start(quint64 requestId,
               const std::filesystem::path&,
               const QStringList&,
               const std::filesystem::path&) override
    {
        QMetaObject::invokeMethod(
            this,
            [this, requestId] {
                emit errorOccurred(
                    requestId,
                    QStringLiteral("This Qt build does not provide process support."));
            },
            Qt::QueuedConnection);
    }

    void requestTermination(std::chrono::milliseconds) override {}
    void stopAndWait(std::chrono::milliseconds, std::chrono::milliseconds) override {}
};

#endif

std::filesystem::path defaultApplicationDirectory()
{
    const auto qtPath = QCoreApplication::applicationDirPath();
    if (!qtPath.isEmpty()) {
        return pathFromQString(qtPath);
    }
    return std::filesystem::current_path();
}

std::filesystem::path canonicalForSecurity(const std::filesystem::path& path,
                                           std::error_code& error)
{
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        return {};
    }
    return std::filesystem::weakly_canonical(absolute, error);
}

bool pathPartEqual(const std::filesystem::path& left, const std::filesystem::path& right)
{
#ifdef _WIN32
    auto leftString = left.native();
    auto rightString = right.native();
    std::transform(leftString.begin(), leftString.end(), leftString.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    std::transform(rightString.begin(), rightString.end(), rightString.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return leftString == rightString;
#else
    return left == right;
#endif
}

bool isWithin(const std::filesystem::path& candidate, const std::filesystem::path& directory)
{
    auto candidatePart = candidate.begin();
    for (auto directoryPart = directory.begin(); directoryPart != directory.end();
         ++directoryPart, ++candidatePart) {
        if (candidatePart == candidate.end() || !pathPartEqual(*candidatePart, *directoryPart)) {
            return false;
        }
    }
    return candidatePart != candidate.end();
}

std::optional<std::array<std::uint64_t, 2>> directoryObjectIdentity(
    const std::filesystem::path& directory)
{
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        directory.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool read = GetFileInformationByHandle(handle, &information) != FALSE;
    CloseHandle(handle);
    if (!read) {
        return std::nullopt;
    }
    const auto fileIndex =
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
        static_cast<std::uint64_t>(information.nFileIndexLow);
    return std::array<std::uint64_t, 2>{
        static_cast<std::uint64_t>(information.dwVolumeSerialNumber), fileIndex};
#else
    struct stat information {};
    if (::lstat(directory.c_str(), &information) != 0) {
        return std::nullopt;
    }
    return std::array<std::uint64_t, 2>{
        static_cast<std::uint64_t>(information.st_dev),
        static_cast<std::uint64_t>(information.st_ino)};
#endif
}

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

double cross2(const Point2& a, const Point2& b, const Point2& c)
{
    return (b.x - a.x) * (c.y - a.y) -
           (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle(const Point2& point,
                     const Point2& a,
                     const Point2& b,
                     const Point2& c,
                     double orientation)
{
    constexpr double epsilon = 1.0e-12;
    return orientation * cross2(a, b, point) >= -epsilon &&
           orientation * cross2(b, c, point) >= -epsilon &&
           orientation * cross2(c, a, point) >= -epsilon;
}

double projectedScale(const std::array<Point2, 4>& points)
{
    double minimumX = points.front().x;
    double maximumX = points.front().x;
    double minimumY = points.front().y;
    double maximumY = points.front().y;
    for (const auto& point : points) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
    }
    return std::max(maximumX - minimumX, maximumY - minimumY);
}

int orientationSign(double value, double areaEpsilon)
{
    if (value > areaEpsilon) {
        return 1;
    }
    if (value < -areaEpsilon) {
        return -1;
    }
    return 0;
}

bool pointOnSegment(const Point2& point,
                    const Point2& start,
                    const Point2& end,
                    double lengthEpsilon)
{
    return point.x >= std::min(start.x, end.x) - lengthEpsilon &&
           point.x <= std::max(start.x, end.x) + lengthEpsilon &&
           point.y >= std::min(start.y, end.y) - lengthEpsilon &&
           point.y <= std::max(start.y, end.y) + lengthEpsilon;
}

bool segmentsIntersect(const Point2& firstStart,
                       const Point2& firstEnd,
                       const Point2& secondStart,
                       const Point2& secondEnd,
                       double areaEpsilon,
                       double lengthEpsilon)
{
    const auto first = orientationSign(
        cross2(firstStart, firstEnd, secondStart), areaEpsilon);
    const auto second = orientationSign(
        cross2(firstStart, firstEnd, secondEnd), areaEpsilon);
    const auto third = orientationSign(
        cross2(secondStart, secondEnd, firstStart), areaEpsilon);
    const auto fourth = orientationSign(
        cross2(secondStart, secondEnd, firstEnd), areaEpsilon);
    if (first != second && third != fourth && first != 0 && second != 0 &&
        third != 0 && fourth != 0) {
        return true;
    }
    return (first == 0 && pointOnSegment(secondStart, firstStart, firstEnd, lengthEpsilon)) ||
           (second == 0 && pointOnSegment(secondEnd, firstStart, firstEnd, lengthEpsilon)) ||
           (third == 0 && pointOnSegment(firstStart, secondStart, secondEnd, lengthEpsilon)) ||
           (fourth == 0 && pointOnSegment(firstEnd, secondStart, secondEnd, lengthEpsilon));
}

bool isSimpleProjectedQuad(const Mesh& mesh, const std::vector<std::uint32_t>& face)
{
    Eigen::Vector3d normal = Eigen::Vector3d::Zero();
    for (std::size_t index = 0; index < face.size(); ++index) {
        const auto current = mesh.positions[face[index]].cast<double>();
        const auto next = mesh.positions[face[(index + 1) % face.size()]].cast<double>();
        normal.x() += (current.y() - next.y()) * (current.z() + next.z());
        normal.y() += (current.z() - next.z()) * (current.x() + next.x());
        normal.z() += (current.x() - next.x()) * (current.y() + next.y());
    }
    if (!normal.allFinite()) {
        return false;
    }
    const auto absolute = normal.cwiseAbs();
    const int axis = absolute.x() >= absolute.y() && absolute.x() >= absolute.z()
                         ? 0
                         : (absolute.y() >= absolute.z() ? 1 : 2);

    std::array<Point2, 4> projected{};
    for (std::size_t index = 0; index < face.size(); ++index) {
        const auto& point = mesh.positions[face[index]];
        projected[index] = axis == 0 ? Point2{point.y(), point.z()}
                           : axis == 1 ? Point2{point.x(), point.z()}
                                       : Point2{point.x(), point.y()};
    }
    const double scale = projectedScale(projected);
    if (!std::isfinite(scale) || scale == 0.0) {
        return false;
    }
    const auto origin = projected.front();
    for (auto& point : projected) {
        point.x = (point.x - origin.x) / scale;
        point.y = (point.y - origin.y) / scale;
    }
    constexpr double lengthEpsilon = 1.0e-12;
    constexpr double areaEpsilon = 1.0e-12;
    double twiceArea = 0.0;
    for (std::size_t index = 0; index < projected.size(); ++index) {
        const auto& current = projected[index];
        const auto& next = projected[(index + 1) % projected.size()];
        twiceArea += current.x * next.y - next.x * current.y;
    }
    if (std::abs(twiceArea) <= areaEpsilon) {
        return false;
    }
    return !segmentsIntersect(projected[0], projected[1], projected[2], projected[3],
                              areaEpsilon, lengthEpsilon) &&
           !segmentsIntersect(projected[1], projected[2], projected[3], projected[0],
                              areaEpsilon, lengthEpsilon);
}

bool securePrivateDirectory(const std::filesystem::path& directory, QString& errorMessage)
{
#ifdef _WIN32
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        errorMessage = QStringLiteral("Windows could not read the current user token (error %1).")
                           .arg(GetLastError());
        return false;
    }

    DWORD tokenBytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || tokenBytes == 0) {
        errorMessage = QStringLiteral("Windows could not size the current user token (error %1).")
                           .arg(GetLastError());
        CloseHandle(token);
        return false;
    }
    std::vector<std::byte> tokenStorage(tokenBytes);
    if (!GetTokenInformation(token, TokenUser, tokenStorage.data(), tokenBytes, &tokenBytes)) {
        errorMessage = QStringLiteral("Windows could not read the current user SID (error %1).")
                           .arg(GetLastError());
        CloseHandle(token);
        return false;
    }
    auto* tokenUser = reinterpret_cast<TOKEN_USER*>(tokenStorage.data());

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_ALL_ACCESS;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    BuildTrusteeWithSidW(&access.Trustee, tokenUser->User.Sid);
    PACL acl = nullptr;
    const auto aclStatus = SetEntriesInAclW(1, &access, nullptr, &acl);
    if (aclStatus != ERROR_SUCCESS) {
        errorMessage = QStringLiteral("Windows could not create a private directory ACL (error %1).")
                           .arg(aclStatus);
        CloseHandle(token);
        return false;
    }

    const auto securityStatus = SetNamedSecurityInfoW(
        const_cast<wchar_t*>(directory.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl, nullptr);
    LocalFree(acl);
    CloseHandle(token);
    if (securityStatus != ERROR_SUCCESS) {
        errorMessage = QStringLiteral("Windows could not secure the private directory (error %1).")
                           .arg(securityStatus);
        return false;
    }
    return true;
#else
    std::error_code permissionError;
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, permissionError);
    if (permissionError) {
        errorMessage = QString::fromStdString(permissionError.message());
        return false;
    }
    return true;
#endif
}

bool triangulateFace(const Mesh& source,
                     const std::vector<std::uint32_t>& face,
                     std::vector<std::array<std::uint32_t, 3>>& triangles)
{
    if (face.size() == 3) {
        triangles.push_back({face[0], face[1], face[2]});
        return true;
    }

    Eigen::Vector3d normal = Eigen::Vector3d::Zero();
    for (std::size_t index = 0; index < face.size(); ++index) {
        const auto current = source.positions[face[index]].cast<double>();
        const auto next = source.positions[face[(index + 1) % face.size()]].cast<double>();
        normal.x() += (current.y() - next.y()) * (current.z() + next.z());
        normal.y() += (current.z() - next.z()) * (current.x() + next.x());
        normal.z() += (current.x() - next.x()) * (current.y() + next.y());
    }

    const auto axis = [&] {
        const auto absolute = normal.cwiseAbs();
        if (absolute.x() >= absolute.y() && absolute.x() >= absolute.z()) {
            return 0;
        }
        return absolute.y() >= absolute.z() ? 1 : 2;
    }();

    std::vector<Point2> projected;
    projected.reserve(face.size());
    for (const auto vertexIndex : face) {
        const auto& point = source.positions[vertexIndex];
        if (axis == 0) {
            projected.push_back({point.y(), point.z()});
        } else if (axis == 1) {
            projected.push_back({point.x(), point.z()});
        } else {
            projected.push_back({point.x(), point.y()});
        }
    }

    double twiceArea = 0.0;
    for (std::size_t index = 0; index < projected.size(); ++index) {
        const auto& current = projected[index];
        const auto& next = projected[(index + 1) % projected.size()];
        twiceArea += current.x * next.y - next.x * current.y;
    }
    constexpr double epsilon = 1.0e-12;
    if (std::abs(twiceArea) <= epsilon) {
        return false;
    }
    const double orientation = twiceArea > 0.0 ? 1.0 : -1.0;

    std::vector<std::size_t> remaining(face.size());
    std::iota(remaining.begin(), remaining.end(), 0);
    while (remaining.size() > 3) {
        bool clipped = false;
        for (std::size_t position = 0; position < remaining.size(); ++position) {
            const auto previous = remaining[(position + remaining.size() - 1) % remaining.size()];
            const auto current = remaining[position];
            const auto next = remaining[(position + 1) % remaining.size()];
            if (orientation * cross2(projected[previous], projected[current], projected[next]) <=
                epsilon) {
                continue;
            }

            bool containsVertex = false;
            for (const auto candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) {
                    continue;
                }
                if (pointInTriangle(projected[candidate], projected[previous],
                                    projected[current], projected[next], orientation)) {
                    containsVertex = true;
                    break;
                }
            }
            if (containsVertex) {
                continue;
            }

            triangles.push_back({face[previous], face[current], face[next]});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(position));
            clipped = true;
            break;
        }
        if (!clipped) {
            return false;
        }
    }
    triangles.push_back({face[remaining[0]], face[remaining[1]], face[remaining[2]]});
    return true;
}

bool isValidAllQuadMesh(const Mesh& mesh)
{
    if (mesh.positions.empty() || mesh.faces.empty()) {
        return false;
    }
    for (const auto& position : mesh.positions) {
        if (!std::isfinite(position.x()) || !std::isfinite(position.y()) ||
            !std::isfinite(position.z())) {
            return false;
        }
    }
    for (const auto& face : mesh.faces) {
        if (face.size() != 4) {
            return false;
        }
        std::unordered_set<std::uint32_t> unique;
        for (const auto index : face) {
            if (index >= mesh.positions.size() || !unique.insert(index).second) {
                return false;
            }
        }
        if (!isSimpleProjectedQuad(mesh, face)) {
            return false;
        }
    }
    return true;
}

} // namespace

RetopoEngine::RetopoEngine(QObject* parent)
    : QObject(parent),
      process_(new QProcessAdapter(this)),
      applicationDirectory_(defaultApplicationDirectory()),
      helperExecutable_(applicationDirectory_ / "engine" / helperExecutableName())
{
    connectProcessSignals();
}

RetopoEngine::RetopoEngine(std::filesystem::path helperExecutable,
                           std::filesystem::path applicationDirectory,
                           QObject* parent)
    : QObject(parent),
      process_(new QProcessAdapter(this)),
      applicationDirectory_(std::move(applicationDirectory)),
      helperExecutable_(std::move(helperExecutable))
{
    connectProcessSignals();
}

RetopoEngine::RetopoEngine(IRetopoProcess* process,
                           std::filesystem::path helperExecutable,
                           std::filesystem::path applicationDirectory,
                           QObject* parent)
    : QObject(parent),
      process_(process),
      applicationDirectory_(std::move(applicationDirectory)),
      helperExecutable_(std::move(helperExecutable))
{
    connectProcessSignals();
}

RetopoEngine::~RetopoEngine()
{
    if (process_ != nullptr) {
        QObject::disconnect(process_, nullptr, this, nullptr);
        process_->stopAndWait(terminationGrace_, shutdownKillWait_);
    }
    (void)cleanupFiles();
}

QStringList RetopoEngine::argumentsFor(const RetopoSettings& settings,
                                       const QString& inputPath,
                                       const QString& outputPath)
{
    QStringList arguments{
        QStringLiteral("-i"), inputPath,
        QStringLiteral("-o"), outputPath,
        QStringLiteral("-f"), QString::number(settings.targetFaces),
    };
    if (settings.preserveFeatures >= 0.5f) {
        arguments.push_back(QStringLiteral("-sharp"));
    }
    return arguments;
}

std::filesystem::path RetopoEngine::helperExecutableName()
{
#ifdef _WIN32
    return L"retoprime-quads.exe";
#else
    return "retoprime-quads";
#endif
}

void RetopoEngine::start(const RetopoRequest& request)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, request] { start(request); }, Qt::QueuedConnection);
        return;
    }
    if (state_ != State::Idle) {
        emit failed(QStringLiteral("Retopology is already running."));
        return;
    }

    const auto sourceValidation = request.source.validate();
    if (!sourceValidation.errors.empty()) {
        emit failed(sourceValidation.errors.join(QLatin1Char('\n')));
        return;
    }
    const auto settingsErrors = request.settings.validate();
    if (!settingsErrors.empty()) {
        QStringList messages;
        for (const auto& error : settingsErrors) {
            messages.push_back(error);
        }
        emit failed(messages.join(QLatin1Char('\n')));
        return;
    }
    if (request.workspace.empty()) {
        emit failed(QStringLiteral("A retopology workspace is required."));
        return;
    }
    if (process_ == nullptr) {
        emit failed(QStringLiteral("The retopology process is unavailable."));
        return;
    }
    if (process_->thread() != thread()) {
        emit failed(QStringLiteral(
            "The retopology process must share the engine thread."));
        return;
    }
    if (!helperIsConfined()) {
        emit failed(QStringLiteral(
            "The retopology helper must be inside the application engine directory."));
        return;
    }

    std::error_code fileError;
    std::filesystem::create_directories(request.workspace, fileError);
    if (fileError) {
        emit failed(QStringLiteral("The retopology workspace could not be created: %1")
                        .arg(QString::fromStdString(fileError.message())));
        return;
    }
    workspaceRoot_ = canonicalForSecurity(request.workspace, fileError);
    if (fileError || !std::filesystem::is_directory(workspaceRoot_, fileError) || fileError) {
        workspaceRoot_.clear();
        emit failed(QStringLiteral("The retopology workspace could not be resolved safely."));
        return;
    }

    bool created = false;
    requestDirectoryIdentity_.clear();
    requestDirectoryObjectIdentity_.reset();
    for (int attempt = 0; attempt < 8 && !created; ++attempt) {
        const auto name = QStringLiteral("retoprime-%1")
                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        requestDirectory_ = workspaceRoot_ / pathFromQString(name);
        created = std::filesystem::create_directory(requestDirectory_, fileError);
        if (fileError) {
            break;
        }
    }
    if (!created) {
        workspaceRoot_.clear();
        requestDirectory_.clear();
        requestDirectoryIdentity_.clear();
        requestDirectoryObjectIdentity_.reset();
        emit failed(QStringLiteral("A private retopology workspace could not be created: %1")
                        .arg(QString::fromStdString(fileError.message())));
        return;
    }
    requestDirectory_ = canonicalForSecurity(requestDirectory_, fileError);
    if (!fileError) {
        requestDirectoryIdentity_ = requestDirectory_;
    }
    if (fileError || !isWithin(requestDirectory_, workspaceRoot_) ||
        !pathPartEqual(requestDirectory_.parent_path(), workspaceRoot_)) {
        const auto cleanupError = cleanupFiles();
        emit failed(cleanupError.isEmpty()
                        ? QStringLiteral("The private retopology workspace was unsafe.")
                        : cleanupError);
        return;
    }
    requestDirectoryObjectIdentity_ = directoryObjectIdentity(requestDirectory_);
    if (!requestDirectoryObjectIdentity_) {
        const auto cleanupError = cleanupFiles();
        emit failed(cleanupError.isEmpty()
                        ? QStringLiteral("The private retopology workspace identity could not be read.")
                        : cleanupError);
        return;
    }
    QString securityError;
    if (!securePrivateDirectory(requestDirectory_, securityError)) {
        const auto cleanupError = cleanupFiles();
        emit failed(QStringLiteral("The private retopology workspace permissions could not be secured: %1 %2")
                        .arg(securityError, cleanupError));
        return;
    }
    const auto securedObjectIdentity = directoryObjectIdentity(requestDirectory_);
    if (!securedObjectIdentity ||
        *securedObjectIdentity != *requestDirectoryObjectIdentity_) {
        const auto cleanupError = cleanupFiles();
        emit failed(cleanupError.isEmpty()
                        ? QStringLiteral("The private retopology workspace identity changed while it was secured.")
                        : cleanupError);
        return;
    }
    requestDirectoryObjectIdentity_ = securedObjectIdentity;

    activeRequestId_ = nextRequestId_++;
    inputPath_ = requestDirectory_ / "input.obj";
    outputPath_ = requestDirectory_ / "output.obj";

    emit progressChanged(0, QStringLiteral("Preparing triangulated input..."));
    QString writeError;
    if (!writeTriangulatedInput(request.source, writeError)) {
        activeRequestId_ = 0;
        const auto cleanupError = cleanupFiles();
        emit failed(cleanupError.isEmpty() ? writeError : writeError + QLatin1Char(' ') + cleanupError);
        return;
    }

    state_ = State::Running;
    emit progressChanged(20, QStringLiteral("Generating quad topology..."));
    process_->start(
        activeRequestId_, helperExecutable_,
        argumentsFor(request.settings, QStringLiteral("input.obj"),
                     QStringLiteral("output.obj")),
        requestDirectory_);
}

void RetopoEngine::cancel()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this] { cancel(); }, Qt::QueuedConnection);
        return;
    }
    if (state_ != State::Running) {
        return;
    }
    state_ = State::Cancelling;
    emit progressChanged(20, QStringLiteral("Stopping retopology..."));
    process_->requestTermination(terminationGrace_);
}

void RetopoEngine::connectProcessSignals()
{
    if (process_ == nullptr) {
        return;
    }
    QObject::connect(process_, &IRetopoProcess::finished,
                     this, &RetopoEngine::processFinished);
    QObject::connect(process_, &IRetopoProcess::errorOccurred,
                     this, &RetopoEngine::processError);
}

void RetopoEngine::processFinished(quint64 requestId, int exitCode)
{
    if (requestId != activeRequestId_ || state_ == State::Idle) {
        return;
    }
    if (state_ == State::Cancelling) {
        state_ = State::Idle;
        activeRequestId_ = 0;
        const auto cleanupError = cleanupFiles();
        emit failed(cleanupError.isEmpty()
                        ? QStringLiteral("Retopology was cancelled.")
                        : QStringLiteral("Retopology was cancelled. %1").arg(cleanupError));
        return;
    }
    if (exitCode != 0) {
        failActive(QStringLiteral("The retopology helper exited with exit code %1.").arg(exitCode));
        return;
    }

    std::error_code outputError;
    if (!std::filesystem::is_regular_file(outputPath_, outputError) || outputError) {
        failActive(QStringLiteral("The retopology helper did not produce an output mesh."));
        return;
    }

    emit progressChanged(80, QStringLiteral("Validating quad output..."));
    try {
        MeshIO meshIO;
        auto output = meshIO.load(outputPath_).mesh;
        if (!isValidAllQuadMesh(output)) {
            failActive(QStringLiteral(
                "The retopology output could not be read as a valid all-quad mesh."));
            return;
        }

        state_ = State::Idle;
        activeRequestId_ = 0;
        const auto cleanupError = cleanupFiles();
        if (!cleanupError.isEmpty()) {
            emit failed(cleanupError);
            return;
        }
        emit progressChanged(100, QStringLiteral("Quad topology complete."));
        emit completed(std::move(output));
    } catch (const std::exception&) {
        failActive(QStringLiteral(
            "The retopology output could not be read as a valid all-quad mesh."));
    }
}

void RetopoEngine::processError(quint64 requestId, const QString& message)
{
    if (requestId != activeRequestId_ || state_ == State::Idle) {
        return;
    }
    if (state_ == State::Cancelling) {
        return;
    }
    failActive(QStringLiteral("The retopology helper could not run: %1").arg(message));
}

void RetopoEngine::failActive(const QString& message)
{
    state_ = State::Idle;
    activeRequestId_ = 0;
    const auto cleanupError = cleanupFiles();
    emit failed(cleanupError.isEmpty() ? message : message + QLatin1Char(' ') + cleanupError);
}

QString RetopoEngine::cleanupFiles()
{
    if (requestDirectory_.empty()) {
        inputPath_.clear();
        outputPath_.clear();
        workspaceRoot_.clear();
        requestDirectoryIdentity_.clear();
        requestDirectoryObjectIdentity_.reset();
        return {};
    }

    // Removal is path-based, so check the no-follow object identity immediately
    // before the existing canonical confinement checks. The private current-user
    // request ACL limits the remaining check/remove interval to the owning user.
    const auto resolvedObjectIdentity = directoryObjectIdentity(requestDirectory_);
    if (!requestDirectoryObjectIdentity_ || !resolvedObjectIdentity ||
        *resolvedObjectIdentity != *requestDirectoryObjectIdentity_) {
        return QStringLiteral("The private retopology workspace could not be cleaned safely.");
    }

    std::error_code canonicalError;
    const auto resolvedDirectory = canonicalForSecurity(requestDirectory_, canonicalError);
    if (canonicalError || workspaceRoot_.empty() || requestDirectoryIdentity_.empty() ||
        !pathPartEqual(resolvedDirectory, requestDirectoryIdentity_) ||
        !isWithin(resolvedDirectory, workspaceRoot_) ||
        !pathPartEqual(resolvedDirectory.parent_path(), workspaceRoot_)) {
        return QStringLiteral("The private retopology workspace could not be cleaned safely.");
    }

    std::error_code removeError;
    std::filesystem::remove_all(resolvedDirectory, removeError);
    std::error_code existsError;
    const bool stillExists = std::filesystem::exists(resolvedDirectory, existsError);
    if (removeError || existsError || stillExists) {
        const auto detail = removeError ? removeError.message() : existsError.message();
        return QStringLiteral("The private retopology workspace could not be removed: %1")
            .arg(QString::fromStdString(detail));
    }

    inputPath_.clear();
    outputPath_.clear();
    requestDirectory_.clear();
    requestDirectoryIdentity_.clear();
    requestDirectoryObjectIdentity_.reset();
    workspaceRoot_.clear();
    return {};
}

bool RetopoEngine::helperIsConfined() const
{
    std::error_code error;
    const auto applicationDirectory = canonicalForSecurity(applicationDirectory_, error);
    if (error) {
        return false;
    }
    const auto engineDirectory = canonicalForSecurity(applicationDirectory_ / "engine", error);
    if (error || !isWithin(engineDirectory, applicationDirectory)) {
        return false;
    }
    const auto helper = canonicalForSecurity(helperExecutable_, error);
    if (error) {
        return false;
    }
    return isWithin(helper, engineDirectory) &&
           pathPartEqual(helper.filename(), helperExecutableName());
}

bool RetopoEngine::writeTriangulatedInput(const Mesh& source, QString& errorMessage) const
{
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (const auto& face : source.faces) {
        if (!triangulateFace(source, face, triangles)) {
            errorMessage = QStringLiteral(
                "A source polygon could not be triangulated safely for retopology.");
            return false;
        }
    }

    std::ofstream output(inputPath_, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        errorMessage = QStringLiteral("The triangulated retopology input could not be written.");
        return false;
    }

    output << std::setprecision(9);
    for (const auto& position : source.positions) {
        output << "v " << position.x() << ' ' << position.y() << ' ' << position.z() << '\n';
    }
    for (const auto& triangle : triangles) {
        output << "f " << (triangle[0] + 1) << ' ' << (triangle[1] + 1) << ' '
               << (triangle[2] + 1) << '\n';
    }

    output.close();
    if (!output) {
        errorMessage = QStringLiteral("The triangulated retopology input could not be written.");
        return false;
    }
    return true;
}

} // namespace retoprime
