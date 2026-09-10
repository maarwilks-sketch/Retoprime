#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "core/RetopoEngine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <Aclapi.h>
#include <Windows.h>
#endif

namespace {

std::filesystem::path nativePath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

std::filesystem::path unicodeChild(const std::filesystem::path& parent)
{
    return parent / nativePath(QStringLiteral("应用-é"));
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                nativePath(QStringLiteral("retoprime-process-%1")
                               .arg(reinterpret_cast<quintptr>(this))))
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

retoprime::Mesh cube()
{
    retoprime::Mesh mesh;
    mesh.positions = {
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f},
    };
    mesh.faces = {
        {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
        {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7},
    };
    return mesh;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMilliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(5);
    }
    return predicate();
}

void stageExecutable(const std::filesystem::path& source,
                     const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing);
#ifndef _WIN32
    std::filesystem::permissions(
        destination,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
#endif
}

class ThreadRecordingProcess final : public retoprime::IRetopoProcess {
public:
    void start(quint64 requestId,
               const std::filesystem::path&,
               const QStringList&,
               const std::filesystem::path&) override
    {
        activeId.store(requestId);
        startThread.store(QThread::currentThread());
    }

    void requestTermination(std::chrono::milliseconds) override
    {
        terminationThread.store(QThread::currentThread());
    }

    void stopAndWait(std::chrono::milliseconds, std::chrono::milliseconds) override
    {
        shutdownThread.store(QThread::currentThread());
    }

    void confirmExit()
    {
        emit finished(activeId.load(), 15);
    }

    std::atomic<quint64> activeId{0};
    std::atomic<QThread*> startThread{nullptr};
    std::atomic<QThread*> terminationThread{nullptr};
    std::atomic<QThread*> shutdownThread{nullptr};
};

#ifdef _WIN32
bool hasProtectedCurrentUserOnlyAcl(const std::filesystem::path& directory)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    DWORD tokenBytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
    std::vector<std::byte> tokenStorage(tokenBytes);
    if (!GetTokenInformation(token, TokenUser, tokenStorage.data(), tokenBytes, &tokenBytes)) {
        CloseHandle(token);
        return false;
    }
    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(tokenStorage.data());

    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto status = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(directory.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor);
    if (status != ERROR_SUCCESS || dacl == nullptr || descriptor == nullptr) {
        CloseHandle(token);
        return false;
    }

    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    const bool protectedDacl =
        GetSecurityDescriptorControl(descriptor, &control, &revision) &&
        (control & SE_DACL_PROTECTED) != 0;
    void* rawAce = nullptr;
    const bool oneAce = dacl->AceCount == 1 && GetAce(dacl, 0, &rawAce);
    bool userOnly = false;
    if (oneAce) {
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
        PSID aceSid = reinterpret_cast<PSID>(
            const_cast<DWORD*>(&ace->SidStart));
        userOnly = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                   EqualSid(aceSid, tokenUser->User.Sid) &&
                   (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS &&
                   (ace->Header.AceFlags & INHERITED_ACE) == 0 &&
                   (ace->Header.AceFlags & OBJECT_INHERIT_ACE) != 0 &&
                   (ace->Header.AceFlags & CONTAINER_INHERIT_ACE) != 0;
    }

    LocalFree(descriptor);
    CloseHandle(token);
    return protectedDacl && userOnly;
}
#endif

} // namespace

TEST_CASE("production process adapter is owned by and moves with the engine")
{
    TemporaryDirectory temp;
    const auto applicationDirectory = temp.path() / "app";
    const auto helper = applicationDirectory / "engine" /
                        retoprime::RetopoEngine::helperExecutableName();
    retoprime::RetopoEngine engine(helper, applicationDirectory);

    const auto processChildren = engine.findChildren<retoprime::IRetopoProcess*>(
        QString(), Qt::FindDirectChildrenOnly);
    REQUIRE(processChildren.size() == 1);
    CHECK(processChildren.front()->parent() == &engine);
    CHECK(processChildren.front()->thread() == engine.thread());
}

TEST_CASE("wrong-thread start and cancel execute the process seam in the engine thread")
{
    TemporaryDirectory temp;
    const auto applicationDirectory = temp.path() / "app";
    const auto helper = applicationDirectory / "engine" /
                        retoprime::RetopoEngine::helperExecutableName();
    std::filesystem::create_directories(helper.parent_path());

    QThread worker;
    auto* process = new ThreadRecordingProcess;
    auto* engine = new retoprime::RetopoEngine(process, helper, applicationDirectory);
    process->setParent(engine);
    engine->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, engine, &QObject::deleteLater);
    worker.start();

    retoprime::RetopoRequest request{cube(), {}, temp.path() / "workspace"};
    engine->start(request);
    REQUIRE(waitUntil([&] { return process->startThread.load() != nullptr; }, 3000));
    CHECK(process->startThread.load() == &worker);

    engine->cancel();
    REQUIRE(waitUntil([&] { return process->terminationThread.load() != nullptr; }, 3000));
    CHECK(process->terminationThread.load() == &worker);

    std::atomic<bool> cancelled{false};
    QObject::connect(engine, &retoprime::RetopoEngine::failed,
                     QCoreApplication::instance(),
                     [&](const QString& message) {
                         cancelled.store(message.contains(QStringLiteral("cancelled")));
                     });
    QMetaObject::invokeMethod(process, [process] { process->confirmExit(); }, Qt::QueuedConnection);
    REQUIRE(waitUntil([&] { return cancelled.load(); }, 3000));

    worker.quit();
    REQUIRE(worker.wait(3000));
}

TEST_CASE("actual adapter cancellation waits for forced exit and cleans Unicode workspace")
{
    TemporaryDirectory temp;
    const auto applicationDirectory = unicodeChild(temp.path()) / "app";
    const auto helper = applicationDirectory / "engine" /
                        retoprime::RetopoEngine::helperExecutableName();
    stageExecutable(nativePath(QString::fromUtf8(RETOPRIME_LONG_HELPER)), helper);

    const auto workspace = unicodeChild(temp.path()) / "workspace";
    retoprime::RetopoEngine engine(helper, applicationDirectory);
    QString failure;
    QObject::connect(&engine, &retoprime::RetopoEngine::failed,
                     [&](const QString& message) { failure = message; });
    retoprime::RetopoRequest request{cube(), {}, workspace};
    engine.start(request);
    REQUIRE(waitUntil([&] {
        std::error_code error;
        return std::filesystem::exists(workspace, error) &&
               std::filesystem::directory_iterator(workspace, error) !=
                   std::filesystem::directory_iterator();
    }, 3000));

#ifdef _WIN32
    std::error_code directoryError;
    auto directory = std::filesystem::directory_iterator(workspace, directoryError);
    REQUIRE_FALSE(directoryError);
    REQUIRE(directory != std::filesystem::directory_iterator());
    CHECK(hasProtectedCurrentUserOnlyAcl(directory->path()));
#endif

    engine.cancel();
    CHECK(failure.isEmpty());
    REQUIRE(waitUntil([&] { return !failure.isEmpty(); }, 6000));
    CHECK_THAT(failure.toStdString(), Catch::Matchers::ContainsSubstring("cancelled"));
    CHECK(std::filesystem::is_empty(workspace));
}

#ifdef RETOPRIME_REAL_HELPER
TEST_CASE("RetopoEngine actual adapter completes with pinned helper through Unicode paths")
{
    TemporaryDirectory temp;
    const auto applicationDirectory = unicodeChild(temp.path()) /
                                      nativePath(QStringLiteral("真实应用"));
    const auto helper = applicationDirectory / "engine" /
                        retoprime::RetopoEngine::helperExecutableName();
    stageExecutable(nativePath(QString::fromUtf8(RETOPRIME_REAL_HELPER)), helper);

    retoprime::RetopoEngine engine(helper, applicationDirectory);
    QString failure;
    std::optional<retoprime::Mesh> result;
    QObject::connect(&engine, &retoprime::RetopoEngine::failed,
                     [&](const QString& message) { failure = message; });
    QObject::connect(&engine, &retoprime::RetopoEngine::completed,
                     [&](const retoprime::Mesh& mesh) { result = mesh; });

    retoprime::RetopoRequest request{
        cube(), {}, unicodeChild(temp.path()) / nativePath(QStringLiteral("工作区"))};
    request.settings.targetFaces = 500;
    request.settings.preserveFeatures = 1.0f;
    engine.start(request);

    REQUIRE(waitUntil([&] { return result.has_value() || !failure.isEmpty(); }, 120000));
    INFO(failure.toStdString());
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->faces.empty());
    CHECK(std::all_of(result->faces.begin(), result->faces.end(),
                      [](const auto& face) { return face.size() == 4; }));
}
#endif
