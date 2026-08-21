#include "core/RetopoEngine.h"
#include "core/MeshIO.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QStringList>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

namespace {

std::filesystem::path qStringPath(const QString& value);

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("retoprime-engine-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct StartedProcess {
    quint64 requestId = 0;
    std::filesystem::path executable;
    QStringList arguments;
    std::filesystem::path workingDirectory;
    std::string inputContents;
};

class FakeProcess final : public retoprime::IRetopoProcess {
public:
    using IRetopoProcess::IRetopoProcess;

    void start(quint64 requestId,
               const std::filesystem::path& executable,
               const QStringList& arguments,
               const std::filesystem::path& workingDirectory) override
    {
        StartedProcess started{requestId, executable, arguments, workingDirectory, {}};
        const auto inputIndex = arguments.indexOf(QStringLiteral("-i"));
        if (inputIndex >= 0 && inputIndex + 1 < arguments.size()) {
            auto inputPath = qStringPath(arguments[inputIndex + 1]);
            if (inputPath.is_relative()) {
                inputPath = workingDirectory / inputPath;
            }
            std::ifstream input(inputPath);
            started.inputContents.assign(
                std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        starts.push_back(std::move(started));
    }

    void requestTermination(std::chrono::milliseconds forceKillAfter) override
    {
        ++terminateCalls;
        terminationTimeouts.push_back(forceKillAfter);
    }

    void stopAndWait(std::chrono::milliseconds gracefulWait,
                     std::chrono::milliseconds killWait) override
    {
        ++stopAndWaitCalls;
        shutdownWaits.emplace_back(gracefulWait, killWait);
    }

    void finish(quint64 requestId, int exitCode)
    {
        emit finished(requestId, exitCode);
    }

    void fail(quint64 requestId, const QString& message)
    {
        emit errorOccurred(requestId, message);
    }

    [[nodiscard]] std::filesystem::path outputPath(std::size_t startIndex) const
    {
        const auto& arguments = starts.at(startIndex).arguments;
        const auto outputIndex = arguments.indexOf(QStringLiteral("-o"));
        REQUIRE(outputIndex >= 0);
        REQUIRE(outputIndex + 1 < arguments.size());
        auto path = qStringPath(arguments[outputIndex + 1]);
        return path.is_relative() ? starts.at(startIndex).workingDirectory / path : path;
    }

    void writeOutput(std::size_t startIndex, const std::string& contents) const
    {
        std::ofstream output(outputPath(startIndex));
        REQUIRE(output.is_open());
        output << contents;
    }

    std::vector<StartedProcess> starts;
    int terminateCalls = 0;
    int stopAndWaitCalls = 0;
    std::vector<std::chrono::milliseconds> terminationTimeouts;
    std::vector<std::pair<std::chrono::milliseconds, std::chrono::milliseconds>> shutdownWaits;
};

struct SignalRecorder {
    explicit SignalRecorder(retoprime::RetopoEngine& engine)
    {
        QObject::connect(&engine, &retoprime::RetopoEngine::progressChanged,
                         [&](int value, const QString& message) {
                             progress.emplace_back(value, message);
                         });
        QObject::connect(&engine, &retoprime::RetopoEngine::completed,
                         [&](const retoprime::Mesh& mesh) { completions.push_back(mesh); });
        QObject::connect(&engine, &retoprime::RetopoEngine::failed,
                         [&](const QString& message) { failures.push_back(message); });
    }

    std::vector<std::pair<int, QString>> progress;
    std::vector<retoprime::Mesh> completions;
    QStringList failures;
};

retoprime::Mesh cubeWithQuadFaces()
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

constexpr auto kQuadOutput =
    "v -1 -1 0\n"
    "v 1 -1 0\n"
    "v 1 1 0\n"
    "v -1 1 0\n"
    "f 1 2 3 4\n";

struct EngineFixture {
    EngineFixture()
        : applicationDirectory(temp.path() / "app"),
          helper(applicationDirectory / "engine" / retoprime::RetopoEngine::helperExecutableName()),
          engine(&process, helper, applicationDirectory),
          observed(engine)
    {
        std::filesystem::create_directories(helper.parent_path());
        request.source = cubeWithQuadFaces();
        request.workspace = temp.path() / "workspace";
    }

    TemporaryDirectory temp;
    std::filesystem::path applicationDirectory;
    std::filesystem::path helper;
    FakeProcess process;
    retoprime::RetopoEngine engine;
    SignalRecorder observed;
    retoprime::RetopoRequest request;
};

std::vector<std::size_t> objFaceSizes(const std::string& contents)
{
    std::vector<std::size_t> sizes;
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.starts_with("f ")) {
            continue;
        }
        std::istringstream face(line.substr(2));
        std::size_t count = 0;
        std::string index;
        while (face >> index) {
            ++count;
        }
        sizes.push_back(count);
    }
    return sizes;
}

std::filesystem::path qStringPath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toStdString());
#endif
}

std::vector<std::array<std::size_t, 3>> objTriangles(const std::string& contents)
{
    std::vector<std::array<std::size_t, 3>> triangles;
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.starts_with("f ")) {
            continue;
        }
        std::istringstream face(line.substr(2));
        std::array<std::size_t, 3> triangle{};
        REQUIRE(face >> triangle[0] >> triangle[1] >> triangle[2]);
        triangles.push_back(triangle);
    }
    return triangles;
}

double signedArea(const Eigen::Vector3f& a,
                  const Eigen::Vector3f& b,
                  const Eigen::Vector3f& c)
{
    return 0.5 * static_cast<double>(
        a.x() * b.y() - b.x() * a.y() +
        b.x() * c.y() - c.x() * b.y() +
        c.x() * a.y() - a.x() * c.y());
}

} // namespace

TEST_CASE("command maps only target faces and feature preservation")
{
    retoprime::RetopoSettings settings;
    settings.targetFaces = 12000;
    settings.preserveFeatures = 0.5f;
    settings.smoothIterations = 10;
    settings.quadRegularity = 0.0f;

    CHECK(retoprime::RetopoEngine::argumentsFor(settings, "in.obj", "out.obj") ==
          QStringList{"-i", "in.obj", "-o", "out.obj", "-f", "12000", "-sharp"});

    settings.preserveFeatures = 0.499f;
    settings.smoothIterations = 0;
    settings.quadRegularity = 1.0f;
    CHECK(retoprime::RetopoEngine::argumentsFor(settings, "in.obj", "out.obj") ==
          QStringList{"-i", "in.obj", "-o", "out.obj", "-f", "12000"});
}

TEST_CASE("invalid source request fails before launching a process")
{
    EngineFixture fixture;
    fixture.request.source = {};

    fixture.engine.start(fixture.request);

    CHECK(fixture.process.starts.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("does not contain mesh geometry"));
}

TEST_CASE("invalid settings fail before launching a process")
{
    EngineFixture fixture;
    fixture.request.settings.targetFaces = 499;

    fixture.engine.start(fixture.request);

    CHECK(fixture.process.starts.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("between 500 and 2,000,000"));
}

TEST_CASE("empty workspace fails before launching a process")
{
    EngineFixture fixture;
    fixture.request.workspace.clear();

    fixture.engine.start(fixture.request);

    CHECK(fixture.process.starts.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("workspace"));
}

TEST_CASE("workspace must resolve to a directory before launching")
{
    EngineFixture fixture;
    fixture.request.workspace = fixture.temp.path() / "workspace-file";
    std::ofstream(fixture.request.workspace) << "occupied";

    fixture.engine.start(fixture.request);

    CHECK(fixture.process.starts.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("workspace"));
}

TEST_CASE("helper executable must remain inside the application engine directory")
{
    TemporaryDirectory temp;
    const auto applicationDirectory = temp.path() / "app";
    const auto outsideHelper = temp.path() / retoprime::RetopoEngine::helperExecutableName();
    FakeProcess process;
    retoprime::RetopoEngine engine(&process, outsideHelper, applicationDirectory);
    SignalRecorder observed(engine);
    retoprime::RetopoRequest request{cubeWithQuadFaces(), {}, temp.path() / "workspace"};

    engine.start(request);

    CHECK(process.starts.empty());
    REQUIRE(observed.failures.size() == 1);
    CHECK_THAT(observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("application engine directory"));
}

TEST_CASE("production process adapter has single QObject ownership under the engine")
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
}

TEST_CASE("engine directory symlink cannot escape the resolved application directory")
{
    TemporaryDirectory temp;
    const auto applicationDirectory = temp.path() / "app";
    const auto outsideEngine = temp.path() / "outside-engine";
    std::filesystem::create_directories(applicationDirectory);
    std::filesystem::create_directories(outsideEngine);
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(
        outsideEngine, applicationDirectory / "engine", symlinkError);
    if (symlinkError) {
        SKIP("Directory symlinks/junctions are unavailable: " + symlinkError.message());
    }

    const auto helper = applicationDirectory / "engine" /
                        retoprime::RetopoEngine::helperExecutableName();
    FakeProcess process;
    retoprime::RetopoEngine engine(&process, helper, applicationDirectory);
    SignalRecorder observed(engine);
    retoprime::RetopoRequest request{cubeWithQuadFaces(), {}, temp.path() / "workspace"};

    engine.start(request);

    CHECK(process.starts.empty());
    REQUIRE(observed.failures.size() == 1);
    CHECK_THAT(observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("application engine directory"));
}

TEST_CASE("second start is rejected while the first request keeps running")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    fixture.engine.start(fixture.request);

    REQUIRE(fixture.process.starts.size() == 1);
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("already running"));

    fixture.process.writeOutput(0, kQuadOutput);
    fixture.process.finish(fixture.process.starts[0].requestId, 0);
    CHECK(fixture.observed.completions.size() == 1);
}

TEST_CASE("source quads are triangulated in the temporary helper input")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);

    REQUIRE(fixture.process.starts.size() == 1);
    const auto faceSizes = objFaceSizes(fixture.process.starts.front().inputContents);
    REQUIRE(faceSizes.size() == 12);
    CHECK(std::all_of(faceSizes.begin(), faceSizes.end(), [](std::size_t size) {
        return size == 3;
    }));
}

TEST_CASE("concave polygon triangulation preserves area orientation and source")
{
    EngineFixture fixture;
    retoprime::Mesh concave;
    concave.positions = {
        {0.0f, 0.0f, 0.0f},
        {3.0f, 0.0f, 0.0f},
        {3.0f, 2.0f, 0.0f},
        {1.5f, 0.75f, 0.0f},
        {0.0f, 2.0f, 0.0f},
    };
    concave.faces = {{0, 1, 2, 3, 4}};
    fixture.request.source = concave;

    fixture.engine.start(fixture.request);

    REQUIRE(fixture.process.starts.size() == 1);
    const auto triangles = objTriangles(fixture.process.starts.front().inputContents);
    REQUIRE(triangles.size() == 3);
    double triangulatedArea = 0.0;
    for (const auto& triangle : triangles) {
        REQUIRE(triangle[0] >= 1);
        REQUIRE(triangle[1] >= 1);
        REQUIRE(triangle[2] >= 1);
        const auto area = signedArea(
            concave.positions[triangle[0] - 1],
            concave.positions[triangle[1] - 1],
            concave.positions[triangle[2] - 1]);
        CHECK(area > 0.0);
        triangulatedArea += area;
    }
    CHECK(triangulatedArea == Catch::Approx(4.125));
    CHECK(fixture.request.source.positions == concave.positions);
    CHECK(fixture.request.source.faces == concave.faces);
}

TEST_CASE("relative Unicode workspace resolves to an isolated absolute private directory")
{
    EngineFixture fixture;
    const auto unicodeWorkspace = fixture.temp.path() /
                                  qStringPath(QStringLiteral("工作区-é"));
    fixture.request.workspace = std::filesystem::relative(
        unicodeWorkspace, std::filesystem::current_path());

    fixture.engine.start(fixture.request);

    REQUIRE(fixture.process.starts.size() == 1);
    const auto& started = fixture.process.starts.front();
    CHECK(started.workingDirectory.is_absolute());
    CHECK(std::filesystem::equivalent(started.workingDirectory.parent_path(), unicodeWorkspace));
    const auto inputIndex = started.arguments.indexOf(QStringLiteral("-i"));
    const auto outputIndex = started.arguments.indexOf(QStringLiteral("-o"));
    REQUIRE(inputIndex >= 0);
    REQUIRE(outputIndex >= 0);
    CHECK(started.arguments[inputIndex + 1] == QStringLiteral("input.obj"));
    CHECK(started.arguments[outputIndex + 1] == QStringLiteral("output.obj"));
    CHECK(qStringPath(started.arguments[inputIndex + 1]).is_relative());
    CHECK(qStringPath(started.arguments[outputIndex + 1]).is_relative());
    CHECK(started.arguments[inputIndex + 1].toLatin1() == started.arguments[inputIndex + 1]);
    CHECK(started.arguments[outputIndex + 1].toLatin1() == started.arguments[outputIndex + 1]);
    CHECK(started.executable == fixture.helper);
}

TEST_CASE("concurrent engines isolate temporary inputs inside one workspace")
{
    EngineFixture first;
    FakeProcess secondProcess;
    retoprime::RetopoEngine secondEngine(
        &secondProcess, first.helper, first.applicationDirectory);
    auto secondRequest = first.request;

    first.engine.start(first.request);
    secondEngine.start(secondRequest);

    REQUIRE(first.process.starts.size() == 1);
    REQUIRE(secondProcess.starts.size() == 1);
    CHECK(first.process.starts.front().workingDirectory !=
          secondProcess.starts.front().workingDirectory);
    CHECK(first.process.starts.front().workingDirectory.parent_path() ==
          secondProcess.starts.front().workingDirectory.parent_path());
}

TEST_CASE("cleanup refuses sibling request redirection and preserves the active sibling")
{
    EngineFixture first;
    FakeProcess secondProcess;
    retoprime::RetopoEngine secondEngine(
        &secondProcess, first.helper, first.applicationDirectory);
    SignalRecorder secondObserved(secondEngine);

    first.engine.start(first.request);
    secondEngine.start(first.request);

    REQUIRE(first.process.starts.size() == 1);
    REQUIRE(secondProcess.starts.size() == 1);
    const auto firstRequestId = first.process.starts.front().requestId;
    const auto firstDirectory = first.process.starts.front().workingDirectory;
    const auto secondDirectory = secondProcess.starts.front().workingDirectory;
    REQUIRE(firstDirectory != secondDirectory);
    std::ofstream(secondDirectory / "sentinel.txt") << "keep";

    std::filesystem::remove_all(firstDirectory);
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(
        secondDirectory, firstDirectory, symlinkError);
    if (symlinkError) {
        SKIP("Directory symlinks/junctions are unavailable: " + symlinkError.message());
    }

    first.process.finish(firstRequestId, 7);

    REQUIRE(first.observed.failures.size() == 1);
    CHECK_THAT(first.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("cleaned safely"));
    CHECK(std::filesystem::is_directory(secondDirectory));
    CHECK(std::filesystem::exists(secondDirectory / "sentinel.txt"));
    CHECK(secondObserved.failures.empty());
}

TEST_CASE("cleanup refuses a sibling directory renamed over the original request path")
{
    EngineFixture first;
    FakeProcess secondProcess;
    retoprime::RetopoEngine secondEngine(
        &secondProcess, first.helper, first.applicationDirectory);
    SignalRecorder secondObserved(secondEngine);

    first.engine.start(first.request);
    secondEngine.start(first.request);

    REQUIRE(first.process.starts.size() == 1);
    REQUIRE(secondProcess.starts.size() == 1);
    const auto firstRequestId = first.process.starts.front().requestId;
    const auto firstDirectory = first.process.starts.front().workingDirectory;
    const auto secondDirectory = secondProcess.starts.front().workingDirectory;
    const auto firstAside = firstDirectory.parent_path() /
                            (firstDirectory.filename().string() + "-aside");
    REQUIRE(firstDirectory != secondDirectory);
    std::ofstream(secondDirectory / "sentinel.txt") << "keep";

    std::error_code renameError;
    std::filesystem::rename(firstDirectory, firstAside, renameError);
    REQUIRE_FALSE(renameError);
    std::filesystem::rename(secondDirectory, firstDirectory, renameError);
    REQUIRE_FALSE(renameError);

    first.process.finish(firstRequestId, 7);

    REQUIRE(first.observed.failures.size() == 1);
    CHECK_THAT(first.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("cleaned safely"));
    CHECK(std::filesystem::is_directory(firstDirectory));
    CHECK(std::filesystem::exists(firstDirectory / "sentinel.txt"));
    CHECK(secondObserved.failures.empty());
}

TEST_CASE("successful helper output completes with a readable quad mesh")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    fixture.process.writeOutput(0, kQuadOutput);
    fixture.process.finish(fixture.process.starts[0].requestId, 0);

    CHECK(fixture.observed.failures.empty());
    REQUIRE(fixture.observed.completions.size() == 1);
    CHECK(fixture.observed.completions.front().validate().quadCount == 1);
    REQUIRE_FALSE(fixture.observed.progress.empty());
    CHECK(fixture.observed.progress.back().first == 100);
}

TEST_CASE("nonzero helper exit fails and preserves the source mesh")
{
    EngineFixture fixture;
    const auto sourceBefore = fixture.request.source;
    fixture.engine.start(fixture.request);
    fixture.process.finish(fixture.process.starts[0].requestId, 7);

    CHECK(fixture.observed.completions.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("exit code 7"));
    CHECK(fixture.request.source.positions == sourceBefore.positions);
    CHECK(fixture.request.source.faces == sourceBefore.faces);
}

TEST_CASE("missing helper output fails without completing")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    fixture.process.finish(fixture.process.starts[0].requestId, 0);

    CHECK(fixture.observed.completions.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("did not produce an output"));
}

TEST_CASE("malformed helper output fails without completing")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    fixture.process.writeOutput(0, "not an OBJ mesh\n");
    fixture.process.finish(fixture.process.starts[0].requestId, 0);

    CHECK(fixture.observed.completions.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("could not be read"));
}

TEST_CASE("parseable output must contain only finite nondegenerate unique-index quads")
{
    const std::vector<std::string> invalidOutputs = {
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\n"
        "f 1 2 3 4\nf 2 5 3\n",
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "f 1 2 2 4\n",
        "v 0 0 0\nv 1 0 0\nv 2 0 0\nv 3 0 0\n"
        "f 1 2 3 4\n",
        "v nan 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "f 1 2 3 4\n",
        "v 0 0 0\nv 2 2 0\nv 0 3 0\nv 3 0 0\n"
        "f 1 2 3 4\n",
    };

    for (const auto& invalidOutput : invalidOutputs) {
        EngineFixture fixture;
        fixture.engine.start(fixture.request);
        fixture.process.writeOutput(0, invalidOutput);
        fixture.process.finish(fixture.process.starts[0].requestId, 0);

        INFO(invalidOutput);
        CHECK(fixture.observed.completions.empty());
        REQUIRE(fixture.observed.failures.size() == 1);
        CHECK_THAT(fixture.observed.failures.front().toStdString(),
                   Catch::Matchers::ContainsSubstring("valid all-quad mesh"));
    }
}

TEST_CASE("simple quad validation is scale relative and preserves either orientation")
{
    const std::array<std::pair<double, std::string>, 3> cases{{
        {1.0e-20,
         "v 0 0 0\nv 1e-20 0 0\n"
         "v 1e-20 1e-20 0\n"
         "v 0 1e-20 0\n"
         "vn 1 0 0\nvn 0 1 0\nvn -1 0 0\nvn 0 -1 0\n"
         "f 1//1 2//2 3//3 4//4\n"},
        {1.0e-4,
         "v 0 0 0\nv 0.0001 0 0\nv 0.0001 0.0001 0\nv 0 0.0001 0\n"
         "f 1 4 3 2\n"},
        {1.0e4,
         "v 0 0 0\nv 10000 0 0\nv 10000 10000 0\nv 0 10000 0\n"
         "f 1 2 3 4\n"},
    }};

    for (const auto& [scale, output] : cases) {
        EngineFixture fixture;
        fixture.engine.start(fixture.request);
        fixture.process.writeOutput(0, output);
        fixture.process.finish(fixture.process.starts.front().requestId, 0);

        INFO(scale);
        INFO(fixture.observed.failures.join(QStringLiteral(" | ")).toStdString());
        CHECK(fixture.observed.failures.empty());
        REQUIRE(fixture.observed.completions.size() == 1);
    }
}

TEST_CASE("process startup error fails without completing")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    fixture.process.fail(fixture.process.starts[0].requestId, QStringLiteral("permission denied"));

    CHECK(fixture.observed.completions.empty());
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("permission denied"));
}

TEST_CASE("cancel waits for confirmed process exit before cleanup or completion")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    REQUIRE(fixture.process.starts.size() == 1);
    const auto requestId = fixture.process.starts[0].requestId;
    const auto privateWorkspace = fixture.process.starts[0].workingDirectory;
    REQUIRE(std::filesystem::exists(privateWorkspace));

    fixture.engine.cancel();
    fixture.engine.cancel();

    CHECK(fixture.process.terminateCalls == 1);
    REQUIRE(fixture.process.terminationTimeouts.size() == 1);
    CHECK(fixture.process.terminationTimeouts.front().count() > 0);
    CHECK(fixture.observed.completions.empty());
    CHECK(fixture.observed.failures.empty());
    CHECK(std::filesystem::exists(privateWorkspace));

    fixture.engine.start(fixture.request);
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("already running"));
    fixture.observed.failures.clear();

    fixture.process.finish(requestId, 15);
    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK(fixture.observed.failures.front() == QStringLiteral("Retopology was cancelled."));
    CHECK_FALSE(std::filesystem::exists(privateWorkspace));
}

TEST_CASE("cleanup refuses a private workspace replaced by an escaping symlink")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    REQUIRE(fixture.process.starts.size() == 1);
    const auto requestId = fixture.process.starts.front().requestId;
    const auto privateWorkspace = fixture.process.starts.front().workingDirectory;
    const auto outside = fixture.temp.path() / "outside";
    std::filesystem::create_directories(outside);
    std::ofstream(outside / "sentinel.txt") << "keep";
    std::filesystem::remove_all(privateWorkspace);
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(outside, privateWorkspace, symlinkError);
    if (symlinkError) {
        SKIP("Directory symlinks/junctions are unavailable: " + symlinkError.message());
    }

    fixture.process.finish(requestId, 7);

    REQUIRE(fixture.observed.failures.size() == 1);
    CHECK_THAT(fixture.observed.failures.front().toStdString(),
               Catch::Matchers::ContainsSubstring("cleaned safely"));
    CHECK(std::filesystem::exists(outside / "sentinel.txt"));
}

TEST_CASE("stale completion cannot complete a newer request")
{
    EngineFixture fixture;
    fixture.engine.start(fixture.request);
    const auto firstRequestId = fixture.process.starts[0].requestId;
    fixture.engine.cancel();
    fixture.process.finish(firstRequestId, 15);

    fixture.engine.start(fixture.request);
    REQUIRE(fixture.process.starts.size() == 2);
    const auto secondRequestId = fixture.process.starts[1].requestId;
    REQUIRE(firstRequestId != secondRequestId);

    fixture.process.writeOutput(1, kQuadOutput);
    fixture.process.finish(firstRequestId, 0);
    CHECK(fixture.observed.completions.empty());

    fixture.process.finish(secondRequestId, 0);
    REQUIRE(fixture.observed.completions.size() == 1);
    CHECK(fixture.observed.completions.front().validate().quadCount == 1);
}

TEST_CASE("destruction stops the process and removes its private workspace without signals")
{
    TemporaryDirectory temp;
    const auto applicationDirectory = temp.path() / "app";
    const auto helper = applicationDirectory / "engine" /
                        retoprime::RetopoEngine::helperExecutableName();
    std::filesystem::create_directories(helper.parent_path());
    FakeProcess process;
    std::filesystem::path privateWorkspace;
    QStringList failures;
    {
        retoprime::RetopoEngine engine(&process, helper, applicationDirectory);
        QObject::connect(&engine, &retoprime::RetopoEngine::failed,
                         [&](const QString& message) { failures.push_back(message); });
        retoprime::RetopoRequest request{cubeWithQuadFaces(), {}, temp.path() / "workspace"};
        engine.start(request);
        REQUIRE(process.starts.size() == 1);
        privateWorkspace = process.starts.front().workingDirectory;
        REQUIRE(std::filesystem::exists(privateWorkspace));
    }

    CHECK(process.stopAndWaitCalls == 1);
    REQUIRE(process.shutdownWaits.size() == 1);
    CHECK(process.shutdownWaits.front().first.count() > 0);
    CHECK(process.shutdownWaits.front().second.count() > 0);
    CHECK(failures.empty());
    CHECK_FALSE(std::filesystem::exists(privateWorkspace));
}

TEST_CASE("real QuadriFlow cube output is readable and contains quads", "[.real-engine]")
{
    const char* outputPath = std::getenv("RETOPRIME_REAL_OUTPUT");
    REQUIRE(outputPath != nullptr);

    retoprime::MeshIO meshIO;
    const auto result = meshIO.load(std::filesystem::path(outputPath));
    const auto validation = result.mesh.validate();
    CHECK(validation.errors.empty());
    REQUIRE(validation.quadCount > 0);
    REQUIRE(validation.quadCount == result.mesh.faces.size());
    CHECK(std::all_of(result.mesh.positions.begin(), result.mesh.positions.end(),
                      [](const auto& position) { return position.allFinite(); }));
    CHECK(std::all_of(result.mesh.faces.begin(), result.mesh.faces.end(), [](const auto& face) {
        if (face.size() != 4) {
            return false;
        }
        const std::unordered_set<std::uint32_t> unique(face.begin(), face.end());
        return unique.size() == 4;
    }));
}
