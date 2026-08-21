# RETOPRIME Standalone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a self-contained Windows application and installer that imports a high-poly FBX/OBJ, generates and displays an editable quad cage, and exports FBX/OBJ without Blender.

**Architecture:** Replace the Python/Blender prototype with a C++20 Qt application. Assimp owns FBX/OBJ interchange, a pinned MIT-licensed QuadriFlow build owns automatic quadrangulation, and a custom OpenGL widget owns the grey high-poly/blue cage viewport and editing. The application invokes only the QuadriFlow helper shipped inside its own installation directory, so the installed product has no external runtime dependency.

**Tech Stack:** C++20, CMake 3.28+, Qt 6 Widgets/OpenGLWidgets, OpenGL 3.3 Core, Assimp, Eigen3, Catch2, QuadriFlow commit `810b7a0967c35b0dc85b4464e3835e26a756c967`, Inno Setup 6, GitHub Actions Windows runners.

**Spec:** `docs/superpowers/specs/2026-08-21-retoprime-standalone-design.md`

## Global Constraints

- Target Windows 10 and Windows 11 x64.
- The installed program must work when Blender and Python are absent.
- Import and export formats for version 1 are FBX and OBJ.
- The source mesh is grey and the generated quad cage is translucent RETOPRIME blue.
- Automatic remeshing must run off the UI thread and support cancellation by terminating the bundled helper process.
- Export is written to a temporary sibling file, validated, then atomically renamed over the requested destination.
- Qt is dynamically linked; installer output must include Qt DLLs and LGPL notices.
- QuadriFlow must be built with `BUILD_FREE_LICENSE=ON`; no Gurobi, SAT helper, or non-redistributable dependency is permitted.
- The final deliverable is `RETOPRIME-Setup.exe`; a portable ZIP is secondary.

---

## File Structure

```text
CMakeLists.txt                         Root targets, tests and install rules
vcpkg.json                            Pinned dependency manifest
cmake/QuadriFlowExternal.cmake        Pinned QuadriFlow helper build
cmake/Warnings.cmake                  MSVC warning policy
src/app/main.cpp                      QApplication entry point
src/app/MainWindow.h/.cpp             Menus, panels, progress and workflow wiring
src/app/RetopoPanel.h/.cpp             Sliders, presets and Generate button
src/core/Mesh.h/.cpp                  Indexed polygon mesh and validation
src/core/MeshIO.h/.cpp                Assimp FBX/OBJ import/export
src/core/RetopoSettings.h/.cpp         Slider values, presets and validation
src/core/RetopoEngine.h/.cpp           Bundled QuadriFlow process adapter
src/core/Symmetry.h/.cpp               Half-mesh preparation, mirroring and welding
src/core/SurfaceProjector.h/.cpp       Cage-to-source nearest-surface projection
src/edit/MeshCommand.h                 Undoable edit interface
src/edit/MoveVerticesCommand.h/.cpp    Vertex movement with reprojection
src/edit/RelaxCommand.h/.cpp           Tangential cage relaxation
src/edit/DensityCommand.h/.cpp         Quad split/patch-collapse density edits
src/edit/CommandStack.h/.cpp           Undo/redo history
src/viewport/Camera.h/.cpp             Orbit/pan/zoom camera
src/viewport/MeshGpuBuffer.h/.cpp      OpenGL mesh upload and draw buffers
src/viewport/ViewportWidget.h/.cpp     Rendering, picking and input
src/viewport/shaders/mesh.vert         Shared mesh vertex shader
src/viewport/shaders/source.frag       Grey source shading
src/viewport/shaders/cage.frag         Blue translucent cage shading
assets/retoprime-logo.svg              Master blue quad-R mark
assets/retoprime.ico                   Multi-resolution Windows icon
assets/retoprime.rc                    EXE icon/version resource
installer/RETOPRIME.iss                Inno Setup installer
installer/THIRD_PARTY_NOTICES.txt      Redistributed component notices
tests/core/*                            Mesh, settings, engine and export tests
tests/edit/*                            Command and topology-edit tests
tests/fixtures/*                        Small deterministic OBJ fixtures
.github/workflows/build-standalone.yml Windows build, test and installer artifact
```

The existing `retoprime/*.py`, `tests/test_core.py`, `RETOPRIME.spec`, `requirements-build.txt`, and `build_windows.bat` are removed only after Task 1's C++ smoke test is green.

---

### Task 1: Native Application Foundation

**Files:**
- Create: `CMakeLists.txt`
- Create: `vcpkg.json`
- Create: `cmake/Warnings.cmake`
- Create: `src/app/main.cpp`
- Create: `tests/core/AppMetadataTests.cpp`
- Remove after green: `retoprime/`, `tests/test_core.py`, `RETOPRIME.spec`, `requirements-build.txt`, `build_windows.bat`

**Interfaces:**
- Produces: `QString retoprime::applicationName()` and `QVersionNumber retoprime::applicationVersion()`.

- [ ] **Step 1: Write the failing metadata test**

```cpp
TEST_CASE("application has RETOPRIME identity") {
    CHECK(retoprime::applicationName() == QStringLiteral("RETOPRIME"));
    CHECK(retoprime::applicationVersion() == QVersionNumber(1, 0, 0));
}
```

- [ ] **Step 2: Configure and verify the test fails**

Run:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug -R AppMetadata --output-on-failure
```

Expected: compilation fails because `retoprime::applicationName` is undefined.

- [ ] **Step 3: Add the native build and minimal entry point**

`main.cpp` must set the identity before constructing the window:

```cpp
namespace retoprime {
QString applicationName() { return QStringLiteral("RETOPRIME"); }
QVersionNumber applicationVersion() { return {1, 0, 0}; }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(retoprime::applicationName());
    QCoreApplication::setApplicationVersion(retoprime::applicationVersion().toString());
    QMainWindow window;
    window.setWindowTitle(retoprime::applicationName());
    window.resize(1280, 800);
    window.show();
    return app.exec();
}
```

The vcpkg manifest contains exact baseline versions for `qtbase`, `assimp`, `eigen3`, and `catch2`.

- [ ] **Step 4: Run the smoke test and launch the app**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug -R AppMetadata --output-on-failure
build\windows-debug\src\app\RETOPRIME.exe
```

Expected: test passes and an empty 1280×800 RETOPRIME window opens.

- [ ] **Step 5: Remove the obsolete Blender prototype and commit**

```powershell
git add -- CMakeLists.txt CMakePresets.json vcpkg.json cmake src tests
git rm -r -- retoprime tests/test_core.py RETOPRIME.spec requirements-build.txt build_windows.bat
git commit -m "build: replace Blender prototype with native Qt foundation"
```

---

### Task 2: Mesh Model and FBX/OBJ Interchange

**Files:**
- Create: `src/core/Mesh.h`
- Create: `src/core/Mesh.cpp`
- Create: `src/core/MeshIO.h`
- Create: `src/core/MeshIO.cpp`
- Create: `tests/core/MeshIOTests.cpp`
- Create: `tests/fixtures/cube-quads.obj`

**Interfaces:**
- Produces: `struct Mesh { std::vector<Eigen::Vector3f> positions; std::vector<Eigen::Vector3f> normals; std::vector<std::vector<uint32_t>> faces; };`
- Produces: `struct MeshLoadResult { Mesh mesh; QStringList warnings; };` and `MeshLoadResult MeshIO::load(const std::filesystem::path&) const`.
- Produces: `void MeshIO::save(const Mesh&, const std::filesystem::path&) const`.
- Produces: `struct MeshValidation { QStringList errors; QStringList warnings; size_t triangleCount; size_t quadCount; };` and `MeshValidation Mesh::validate() const`.
- Produces: `Mesh Mesh::safePreprocess() const`, removing degenerate faces and unreferenced vertices while preserving the original mesh.

- [ ] **Step 1: Write failing OBJ round-trip and validation tests**

```cpp
TEST_CASE("quad OBJ round trip preserves one quad") {
    retoprime::MeshIO io;
    const auto loaded = io.load(fixture("cube-quads.obj"));
    REQUIRE(loaded.mesh.faces.size() == 6);
    CHECK(loaded.mesh.validate().quadCount == 6);
    const auto output = tempPath("cube-roundtrip.obj");
    io.save(loaded.mesh, output);
    CHECK(io.load(output).mesh.validate().quadCount == 6);
}

TEST_CASE("unsupported extension is rejected without changing state") {
    retoprime::MeshIO io;
    CHECK_THROWS_WITH(io.load("mesh.stl"), Catch::Matchers::ContainsSubstring("FBX or OBJ"));
}
```

- [ ] **Step 2: Run and observe missing MeshIO failures**

Run: `ctest --preset windows-debug -R MeshIO --output-on-failure`  
Expected: compilation fails because `MeshIO` and `Mesh` do not exist.

- [ ] **Step 3: Implement indexed mesh conversion through Assimp**

Import uses `aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_ValidateDataStructure | aiProcess_SortByPType`. Preserve polygon index counts when converting `aiFace`; triangulate only in `MeshGpuBuffer` and in the temporary QuadriFlow input.

Validation returns exact messages:

```cpp
if (positions.empty() || faces.empty())
    result.errors.push_back("The file does not contain mesh geometry.");
if (face.size() < 3)
    result.errors.push_back("A face has fewer than three vertices.");
if (index >= positions.size())
    result.errors.push_back("A face references a missing vertex.");
```

`safePreprocess()` removes duplicate indices within a face, faces with fewer than three distinct vertices, zero-area faces, and then unreferenced vertices. It returns a new mesh so cancelling the warning dialog leaves the source unchanged.

- [ ] **Step 4: Run MeshIO tests**

Run: `ctest --preset windows-debug -R MeshIO --output-on-failure`  
Expected: all MeshIO tests pass.

- [ ] **Step 5: Commit**

```powershell
git add -- src/core tests/core tests/fixtures CMakeLists.txt
git commit -m "feat: add FBX and OBJ mesh interchange"
```

---

### Task 3: Retopology Settings, Sliders and Presets

**Files:**
- Create: `src/core/RetopoSettings.h`
- Create: `src/core/RetopoSettings.cpp`
- Create: `tests/core/RetopoSettingsTests.cpp`

**Interfaces:**
- Produces: `enum class SymmetryAxis { Off, X, Y, Z };`
- Produces: `enum class RetopoPreset { GameProp, Character, HeroAsset, FilmHighDetail, Custom };`
- Produces: `struct RetopoSettings` with the ten approved slider values and `std::vector<QString> validate() const`.
- Produces: `RetopoSettings settingsForPreset(RetopoPreset)`.

- [ ] **Step 1: Write failing preset and range tests**

```cpp
TEST_CASE("character preset starts at 25000 faces") {
    const auto settings = settingsForPreset(RetopoPreset::Character);
    CHECK(settings.targetFaces == 25000);
    CHECK(settings.preserveFeatures >= 0.5f);
}

TEST_CASE("face count range is enforced") {
    RetopoSettings settings;
    settings.targetFaces = 499;
    CHECK(settings.validate() == std::vector<QString>{
        QStringLiteral("Target polygon count must be between 500 and 2,000,000.")});
}
```

- [ ] **Step 2: Verify both tests fail**

Run: `ctest --preset windows-debug -R RetopoSettings --output-on-failure`  
Expected: compilation fails because `RetopoSettings` is undefined.

- [ ] **Step 3: Implement exact settings model**

```cpp
struct RetopoSettings {
    int targetFaces = 25000;
    float topologyDensity = 0.5f;
    float minimumFaceSize = 0.05f;
    float maximumFaceSize = 1.0f;
    float adaptiveDetail = 0.5f;
    float projectionStrength = 1.0f;
    int smoothIterations = 2;
    float preserveFeatures = 0.75f;
    float sharpEdgeDegrees = 60.0f;
    float quadRegularity = 1.0f;
    float symmetryTolerance = 0.001f;
    SymmetryAxis symmetryAxis = SymmetryAxis::Off;
};
```

All normalised sliders validate in `[0, 1]`; face-size values must be positive and minimum cannot exceed maximum; smooth iterations validate in `[0, 10]`.

- [ ] **Step 4: Run the settings tests**

Run: `ctest --preset windows-debug -R RetopoSettings --output-on-failure`  
Expected: all settings tests pass.

- [ ] **Step 5: Commit**

```powershell
git add -- src/core/RetopoSettings.* tests/core/RetopoSettingsTests.cpp CMakeLists.txt
git commit -m "feat: define retopology sliders and presets"
```

---

### Task 4: Bundled QuadriFlow Engine

**Files:**
- Create: `cmake/QuadriFlowExternal.cmake`
- Create: `src/core/RetopoEngine.h`
- Create: `src/core/RetopoEngine.cpp`
- Create: `tests/core/RetopoEngineTests.cpp`
- Create: `tests/fixtures/tetra.obj`
- Create: `installer/licenses/QuadriFlow-LICENSE.txt`

**Interfaces:**
- Consumes: `MeshIO`, `Mesh`, `RetopoSettings`.
- Produces: `RetopoRequest { Mesh source; RetopoSettings settings; std::filesystem::path workspace; }`.
- Produces: Qt signals `progressChanged(int, QString)`, `completed(Mesh)`, and `failed(QString)`.
- Produces: `void RetopoEngine::start(const RetopoRequest&)` and `void RetopoEngine::cancel()`.

- [ ] **Step 1: Write failing command-construction tests**

```cpp
TEST_CASE("engine command maps target and sharp preservation") {
    RetopoSettings settings;
    settings.targetFaces = 12000;
    settings.preserveFeatures = 0.8f;
    const auto args = RetopoEngine::argumentsFor(settings, "in.obj", "out.obj");
    CHECK(args == QStringList{"-i", "in.obj", "-o", "out.obj", "-f", "12000", "-sharp"});
}

TEST_CASE("cancel terminates a running helper") {
    FakeProcess process;
    RetopoEngine engine(&process);
    engine.start(validRequest());
    engine.cancel();
    CHECK(process.terminateCount() == 1);
}
```

- [ ] **Step 2: Verify failures**

Run: `ctest --preset windows-debug -R RetopoEngine --output-on-failure`  
Expected: compilation fails because `RetopoEngine` is undefined.

- [ ] **Step 3: Add the pinned engine build**

`QuadriFlowExternal.cmake` uses `ExternalProject_Add` with:

```cmake
GIT_REPOSITORY https://github.com/hjwdzh/QuadriFlow.git
GIT_TAG 810b7a0967c35b0dc85b4464e3835e26a756c967
CMAKE_ARGS
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_FREE_LICENSE=ON
  -DBUILD_GUROBI=OFF
  -DBUILD_LOG=OFF
```

Copy the resulting binary to `$<TARGET_FILE_DIR:RETOPRIME>/engine/retoprime-quads.exe`. The installer includes it at `{app}\engine\retoprime-quads.exe` and reproduces the MIT notice.

- [ ] **Step 4: Implement conversion and background execution**

`start()` writes a triangulated temporary OBJ, launches the bundled helper through `QProcess`, checks both exit code and output existence, imports the resulting quad OBJ, and emits `completed`. It must reject an executable path outside `QCoreApplication::applicationDirPath()/engine`.

Smoothness maps to a post-engine `SurfaceProjector::relaxAndProject` call, not an unsupported QuadriFlow flag. `quadRegularity < 0.5` changes only post-processing relaxation; target count and sharp preservation are the only settings sent directly to the helper.

- [ ] **Step 5: Run unit and real-engine fixture tests**

Run:

```powershell
cmake --build --preset windows-release --target quadriflow_external RETOPRIME_tests
ctest --preset windows-release -R RetopoEngine --output-on-failure
```

Expected: command tests pass; real engine turns `tetra.obj` into a readable OBJ with at least one quad or reports the documented small-mesh limitation without crashing.

- [ ] **Step 6: Commit**

```powershell
git add -- cmake/QuadriFlowExternal.cmake src/core/RetopoEngine.* tests/core tests/fixtures installer/licenses CMakeLists.txt
git commit -m "feat: bundle standalone QuadriFlow engine"
```

---

### Task 5: Symmetry, Projection and Mesh Validation

**Files:**
- Create: `src/core/Symmetry.h`
- Create: `src/core/Symmetry.cpp`
- Create: `src/core/SurfaceProjector.h`
- Create: `src/core/SurfaceProjector.cpp`
- Create: `tests/core/SymmetryTests.cpp`
- Create: `tests/core/SurfaceProjectorTests.cpp`

**Interfaces:**
- Produces: `Mesh Symmetry::extractPositiveHalf(const Mesh&, SymmetryAxis, float tolerance)`.
- Produces: `Mesh Symmetry::mirrorAndWeld(const Mesh&, SymmetryAxis, float tolerance)`.
- Produces: `void SurfaceProjector::project(Mesh& cage, const Mesh& source, std::span<const uint32_t> vertices, float strength)`.
- Produces: `void SurfaceProjector::relaxAndProject(Mesh& cage, const Mesh& source, int iterations, float strength)`.

- [ ] **Step 1: Write failing symmetry and projection tests**

```cpp
TEST_CASE("X mirror welds centre vertices") {
    const Mesh half = halfQuadOnPositiveX();
    const Mesh full = Symmetry::mirrorAndWeld(half, SymmetryAxis::X, 0.001f);
    CHECK(full.positions.size() == 6);
    CHECK(full.faces.size() == 2);
    CHECK(full.validate().errors.empty());
}

TEST_CASE("projection places cage point on source triangle") {
    Mesh cage = onePoint({0.2f, 0.2f, 2.0f});
    const std::array<uint32_t, 1> selected{0};
    SurfaceProjector{}.project(cage, unitTriangleAtZ0(), selected, 1.0f);
    CHECK(cage.positions[0].z() == Catch::Approx(0.0f));
}
```

- [ ] **Step 2: Run and verify failures**

Run: `ctest --preset windows-debug -R "Symmetry|SurfaceProjector" --output-on-failure`  
Expected: missing class compilation failures.

- [ ] **Step 3: Implement deterministic geometry operations**

Use a BVH over source triangles for nearest-point projection. Symmetry snaps vertices within tolerance to the plane, duplicates the chosen half with reversed polygon winding, and welds quantised plane positions. Run `Mesh::validate()` after every operation.

- [ ] **Step 4: Connect symmetry to RetopoEngine**

For an enabled symmetry axis: extract the positive half before writing the engine OBJ; mirror and weld the result; project all output vertices back to the full source.

- [ ] **Step 5: Run core tests**

Run: `ctest --preset windows-debug -R "Symmetry|SurfaceProjector|RetopoEngine" --output-on-failure`  
Expected: all selected tests pass.

- [ ] **Step 6: Commit**

```powershell
git add -- src/core tests/core CMakeLists.txt
git commit -m "feat: add cage symmetry and surface projection"
```

---

### Task 6: Grey High-Poly and Blue Quad-Cage Viewport

**Files:**
- Create: `src/viewport/Camera.h`
- Create: `src/viewport/Camera.cpp`
- Create: `src/viewport/MeshGpuBuffer.h`
- Create: `src/viewport/MeshGpuBuffer.cpp`
- Create: `src/viewport/ViewportWidget.h`
- Create: `src/viewport/ViewportWidget.cpp`
- Create: `src/viewport/shaders/mesh.vert`
- Create: `src/viewport/shaders/source.frag`
- Create: `src/viewport/shaders/cage.frag`
- Create: `tests/core/CameraTests.cpp`

**Interfaces:**
- Consumes: `Mesh`.
- Produces: `void ViewportWidget::setSourceMesh(std::shared_ptr<const Mesh>)`.
- Produces: `void ViewportWidget::setCageMesh(std::shared_ptr<Mesh>)`.
- Produces: `ViewportDisplay { bool source, cage, wireframe, xray, splitComparison; }`.
- Produces: signals `selectionChanged(Selection)` and `cageEdited()`.

- [ ] **Step 1: Write failing camera framing test**

```cpp
TEST_CASE("frame all fits mesh bounds") {
    Camera camera;
    camera.setViewport(1280, 720);
    camera.frame(Aabb{{-1,-1,-1}, {1,1,1}});
    CHECK(camera.nearPlane() > 0.0f);
    CHECK(camera.farPlane() > camera.nearPlane());
    CHECK(camera.project({1,1,1}).has_value());
}
```

- [ ] **Step 2: Verify the camera test fails**

Run: `ctest --preset windows-debug -R Camera --output-on-failure`  
Expected: compilation fails because `Camera` is undefined.

- [ ] **Step 3: Implement camera and GPU buffers**

Triangulate polygons only for shaded index buffers. Build a separate unique-edge buffer from sorted vertex-index pairs so quad wireframes do not show internal triangulation diagonals.

- [ ] **Step 4: Implement the approved viewport appearance and controls**

Source fragment colour: `vec4(0.62, 0.64, 0.67, 1.0)`. Cage face colour: `vec4(0.10, 0.53, 0.88, 0.42)`. Cage edges: `vec4(0.015, 0.035, 0.055, 1.0)`. Enable depth testing; render source first, translucent cage second, edges third.

Left-drag orbits, middle-drag pans, wheel zooms, `F` frames, and `1/2/3` select vertex/edge/face modes. Picking uses an off-screen integer-ID framebuffer.

- [ ] **Step 5: Add an off-screen render smoke test**

Launch with `QT_QPA_PLATFORM=offscreen`, render the cube source/cage to a `QImage`, and assert the centre pixel is non-background and that no OpenGL error is returned.

- [ ] **Step 6: Run viewport tests and inspect a screenshot**

Run:

```powershell
ctest --preset windows-debug -R "Camera|Viewport" --output-on-failure
build\windows-debug\src\app\RETOPRIME.exe tests\fixtures\cube-quads.obj
```

Expected: tests pass; viewport shows a grey cube with a readable blue cage.

- [ ] **Step 7: Commit**

```powershell
git add -- src/viewport tests/core CMakeLists.txt
git commit -m "feat: render source mesh and editable blue quad cage"
```

---

### Task 7: Cage Editing, Density Brush and Undo/Redo

**Files:**
- Create: `src/edit/MeshCommand.h`
- Create: `src/edit/MoveVerticesCommand.h`
- Create: `src/edit/MoveVerticesCommand.cpp`
- Create: `src/edit/RelaxCommand.h`
- Create: `src/edit/RelaxCommand.cpp`
- Create: `src/edit/DensityCommand.h`
- Create: `src/edit/DensityCommand.cpp`
- Create: `src/edit/CommandStack.h`
- Create: `src/edit/CommandStack.cpp`
- Create: `tests/edit/CommandStackTests.cpp`
- Create: `tests/edit/DensityCommandTests.cpp`

**Interfaces:**
- Consumes: `Mesh`, `SurfaceProjector`, viewport `Selection`.
- Produces: `class MeshCommand { virtual void apply(Mesh&) = 0; virtual void revert(Mesh&) = 0; }`.
- Produces: `void CommandStack::execute(std::unique_ptr<MeshCommand>, Mesh&)`, `undo(Mesh&)`, `redo(Mesh&)`.
- Produces: `DensityCommand::increase(faceIds)` and `DensityCommand::decrease(patchIds)`.

- [ ] **Step 1: Write failing undo and quad-density tests**

```cpp
TEST_CASE("undo restores moved vertex") {
    Mesh mesh = singleQuad();
    CommandStack stack;
    stack.execute(std::make_unique<MoveVerticesCommand>(
        std::vector<uint32_t>{0}, Eigen::Vector3f{1,0,0}), mesh);
    stack.undo(mesh);
    CHECK(mesh.positions[0] == Eigen::Vector3f{0,0,0});
}

TEST_CASE("increase density splits one quad into four quads") {
    Mesh mesh = singleQuad();
    DensityCommand::increase({0}).apply(mesh);
    CHECK(mesh.faces.size() == 4);
    CHECK(mesh.validate().quadCount == 4);
}
```

- [ ] **Step 2: Verify the edit tests fail**

Run: `ctest --preset windows-debug -R "CommandStack|DensityCommand" --output-on-failure`  
Expected: missing edit classes.

- [ ] **Step 3: Implement command history and movement**

Each command stores only changed vertices/faces plus their original values. Moving vertices projects them to the source using the approved projection-strength slider. Mirrored movement resolves the counterpart through the symmetry map.

- [ ] **Step 4: Implement density increase and decrease**

Increase inserts four edge midpoints and one face centre, replacing each selected quad with four quads while sharing midpoints across adjacent selected faces. Decrease accepts only a regular 2×2 quad patch with four boundary corners; it replaces the patch with one quad. Invalid decrease patches return `"Decrease density needs a complete 2 by 2 quad patch."` and do not modify the mesh.

- [ ] **Step 5: Implement relaxation**

Relax uses tangential Laplacian smoothing on selected cage vertices and reprojects after each iteration. Boundary vertices remain on the boundary unless explicitly selected with Alt.

- [ ] **Step 6: Run all edit tests**

Run: `ctest --preset windows-debug -R "CommandStack|DensityCommand|Relax" --output-on-failure`  
Expected: all edit tests pass.

- [ ] **Step 7: Wire viewport manipulation and commit**

```powershell
git add -- src/edit src/viewport tests/edit CMakeLists.txt
git commit -m "feat: add projected cage editing and density tools"
```

---

### Task 8: Complete RETOPRIME Interface and Workflow

**Files:**
- Create: `src/app/MainWindow.h`
- Create: `src/app/MainWindow.cpp`
- Create: `src/app/RetopoPanel.h`
- Create: `src/app/RetopoPanel.cpp`
- Create: `src/app/RecoveryStore.h`
- Create: `src/app/RecoveryStore.cpp`
- Create: `src/app/retoprime.qss`
- Create: `tests/core/MainWindowTests.cpp`
- Create: `tests/core/RecoveryStoreTests.cpp`
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: `MeshIO`, `RetopoEngine`, `ViewportWidget`, `CommandStack`, `RetopoSettings`.
- Produces: actions `importMesh()`, `generateCage()`, `cancelGeneration()`, `exportCage()`, `undo()`, and `redo()`.
- Produces: `void RecoveryStore::save(const std::filesystem::path& sourcePath, const Mesh* cage)` and `std::optional<RecoverySession> RecoveryStore::load()`.

- [ ] **Step 1: Write failing UI contract test**

```cpp
TEST_CASE("main window exposes the retopology workflow") {
    MainWindow window;
    CHECK(window.findChild<QAction*>("importAction") != nullptr);
    CHECK(window.findChild<QPushButton*>("generateButton")->text() == "GENERATE QUAD CAGE");
    CHECK(window.findChild<QSlider*>("targetFacesSlider") != nullptr);
    CHECK(window.findChild<ViewportWidget*>("viewport") != nullptr);
}
```

- [ ] **Step 2: Verify the UI test fails**

Run: `ctest --preset windows-debug -R MainWindow --output-on-failure`  
Expected: compilation fails because `MainWindow` is undefined.

- [ ] **Step 3: Build the approved layout**

Use a central `ViewportWidget`, a fixed 320 px right `RetopoPanel`, a 48 px left tool bar, top menu/quick actions, and bottom status bar. Create named widgets for all ten approved controls. Slider movement updates the face estimate label only; `GENERATE QUAD CAGE` begins processing.

- [ ] **Step 4: Implement workflow state rules**

States are `Empty`, `SourceLoaded`, `Generating`, and `CageReady`. Export and edit tools are enabled only in `CageReady`; Cancel is enabled only in `Generating`; importing during `Generating` requires cancellation confirmation. Failures preserve the last source and cage.

The Display section wires High Poly, Cage, Wireframe, X-Ray and Before/After Split check boxes to `ViewportDisplay`. The status bar reports source triangles, cage quads, selected element count and generation stage.

- [ ] **Step 5: Implement safe export**

Export to `<destination>.retoprime-tmp`, reload and validate it, then use `QSaveFile` semantics to replace the requested destination. Show the exact success message `Exported <face count> faces to <path>.`.

- [ ] **Step 6: Add crash recovery**

Write the last source path and cage arrays to `QStandardPaths::AppLocalDataLocation/recovery/session.bin` after import and every completed edit command. Use `QDataStream` with magic `0x52545052` and format version `1`. On startup, offer Restore only when the source path still exists and the cage validates; otherwise delete the invalid recovery record. A normal clean close deletes the record after the user confirms unsaved edits can be discarded.

- [ ] **Step 7: Run UI tests and manual workflow**

Run:

```powershell
ctest --preset windows-debug -R "MainWindow|RecoveryStore|RetopoSettings|MeshIO" --output-on-failure
build\windows-debug\src\app\RETOPRIME.exe
```

Expected: import, Generate, Cancel, edit, undo/redo and export controls follow the state rules; the UI remains responsive during generation.

- [ ] **Step 8: Commit**

```powershell
git add -- src/app tests/core CMakeLists.txt
git commit -m "feat: complete RETOPRIME desktop workflow"
```

---

### Task 9: Identifiable Quad-R Logo and Windows Resources

**Files:**
- Create: `assets/retoprime-logo.svg`
- Create: `assets/retoprime-icon-small.svg`
- Create: `assets/retoprime.ico`
- Create: `assets/retoprime.rc`
- Create: `tools/build_icon.py`
- Create: `tests/core/IconTests.cpp`
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: embedded resource ID `IDI_RETOPRIME_ICON` and Qt resource path `:/branding/retoprime-logo.svg`.

- [ ] **Step 1: Write failing icon resource test**

```cpp
TEST_CASE("application icon is available at small and large sizes") {
    const QIcon icon(QStringLiteral(":/branding/retoprime.ico"));
    CHECK(!icon.pixmap(16, 16).isNull());
    CHECK(!icon.pixmap(256, 256).isNull());
}
```

- [ ] **Step 2: Verify the icon test fails**

Run: `ctest --preset windows-debug -R Icon --output-on-failure`  
Expected: the icon resource pixmaps are null.

- [ ] **Step 3: Create the master vector mark**

The SVG uses a 256×256 dark rounded square (`#111722`), a bold R silhouette filled with RETOPRIME blue (`#178BEA`), dark internal quad edges, and circular node accents. The small SVG removes internal lines thinner than 8 px so the R remains identifiable at 16 px.

`build_icon.py` renders 16, 24, 32, 48, 64, 128 and 256 px frames into one ICO. It asserts every requested size exists before replacing `assets/retoprime.ico`.

- [ ] **Step 4: Embed the icon everywhere**

Set `QApplication::setWindowIcon(QIcon(":/branding/retoprime.ico"))`, add `retoprime.rc` to the Windows executable target, and set `ApplicationIcon={#SourcePath}\..\assets\retoprime.ico` in Inno Setup.

- [ ] **Step 5: Run icon tests and inspect Windows shell appearance**

Run: `ctest --preset windows-debug -R Icon --output-on-failure`  
Expected: tests pass and the EXE, taskbar and Alt-Tab show the blue quad-R.

- [ ] **Step 6: Commit**

```powershell
git add -- assets tools src/app/main.cpp tests/core/IconTests.cpp CMakeLists.txt
git commit -m "feat: add identifiable RETOPRIME quad-R icon"
```

---

### Task 10: Installer, Clean-Machine Verification and Delivery

**Files:**
- Create: `installer/RETOPRIME.iss`
- Create: `installer/THIRD_PARTY_NOTICES.txt`
- Create: `installer/licenses/Assimp-LICENSE.txt`
- Create: `installer/licenses/Qt-LGPL-3.0.txt`
- Create: `tests/install/VerifyInstall.ps1`
- Create: `.github/workflows/build-standalone.yml`
- Modify: `README.md`
- Remove: `.github/workflows/build-windows.yml`

**Interfaces:**
- Produces: `artifacts/RETOPRIME-Setup.exe` and `artifacts/RETOPRIME-Portable.zip`.

- [ ] **Step 1: Write failing installed-layout verification**

`VerifyInstall.ps1` must assert these exact files after a silent install:

```powershell
$required = @(
  "$InstallRoot\RETOPRIME.exe",
  "$InstallRoot\engine\retoprime-quads.exe",
  "$InstallRoot\Qt6Core.dll",
  "$InstallRoot\Qt6Gui.dll",
  "$InstallRoot\Qt6Widgets.dll",
  "$InstallRoot\THIRD_PARTY_NOTICES.txt"
)
$missing = $required | Where-Object { -not (Test-Path $_) }
if ($missing) { throw "Missing installed files: $($missing -join ', ')" }
```

- [ ] **Step 2: Run it before installer creation and observe failure**

Run: `pwsh tests/install/VerifyInstall.ps1 -InstallRoot "$env:TEMP\RetoprimeTest"`  
Expected: failure listing missing files.

- [ ] **Step 3: Implement CMake install rules and Inno Setup**

Run `windeployqt --release --no-translations` into the staging folder. Include RETOPRIME, the engine helper, Assimp DLLs, Qt DLLs/plugins, licences and icon. Installer identity:

```ini
AppId={{9E034921-72E4-4D6A-80BC-91D7486B4B89}
AppName=RETOPRIME
AppVersion=1.0.0
DefaultDirName={autopf}\RETOPRIME
OutputBaseFilename=RETOPRIME-Setup
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\RETOPRIME.exe
```

Offer Start menu and optional desktop shortcuts. Do not require administrator rights if the user selects the per-user install mode.

- [ ] **Step 4: Add the Windows release workflow**

The workflow checks out recursively, restores vcpkg cache, configures MSVC x64 Release, runs all tests, builds QuadriFlow with the pinned commit, stages dependencies, compiles Inno Setup, silently installs into the runner, runs `VerifyInstall.ps1`, runs an engine OBJ fixture with no Blender present, uninstalls, and uploads both artifacts with SHA-256 files.

- [ ] **Step 5: Run full release verification**

Run:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --config Release
ctest --preset windows-release --output-on-failure
cmake --install build\windows-release --prefix stage
iscc installer\RETOPRIME.iss
pwsh tests\install\VerifyInstall.ps1 -Installer artifacts\RETOPRIME-Setup.exe
Get-FileHash artifacts\RETOPRIME-Setup.exe -Algorithm SHA256
```

Expected: all tests pass; install verification imports a fixture, generates a quad OBJ, exports it, and uninstalls without Blender installed.

- [ ] **Step 6: Update documentation and commit**

README must state the exact supported formats, minimum OS, install steps, slider descriptions, editing controls, third-party engine credits, and that no Blender installation is required.

```powershell
git add -- installer tests/install .github/workflows/build-standalone.yml README.md CMakeLists.txt
git rm -- .github/workflows/build-windows.yml
git commit -m "release: build standalone RETOPRIME installer"
```

- [ ] **Step 7: Final release checklist**

Verify and record:

```text
[ ] Windows 10 x64 clean install
[ ] Windows 11 x64 clean install
[ ] No Blender executable on PATH or in Program Files
[ ] FBX import -> quad generation -> FBX export
[ ] OBJ import -> quad generation -> OBJ export
[ ] Grey source and blue cage render correctly
[ ] Sliders and presets alter the requested settings
[ ] Vertex/edge/face move, relax, density increase/decrease
[ ] Undo and redo
[ ] Cancellation preserves previous cage
[ ] Corrupt input and failed export do not destroy user data
[ ] EXE, taskbar, Start menu and installer show the quad-R icon
[ ] Installer uninstall removes application files and retains user projects
[ ] RETOPRIME-Setup.exe and SHA-256 delivered
```
