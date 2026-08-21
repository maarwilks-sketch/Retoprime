#include "core/Mesh.h"
#include "core/MeshIO.h"

#include <QCoreApplication>
#include <QStringList>

#include <assimp/Exporter.hpp>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path fixture(const std::string& name)
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / name;
}

std::filesystem::path tempPath(const std::string& name)
{
    const auto uniqueId = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("retoprime-" + uniqueId + "-" + name);
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

bool hasExportExtension(const char* extension)
{
    Assimp::Exporter exporter;
    for (size_t index = 0; index < exporter.GetExportFormatCount(); ++index) {
        const aiExportFormatDesc* format = exporter.GetExportFormatDescription(index);
        if (format != nullptr && format->fileExtension != nullptr &&
            std::string_view(format->fileExtension) == extension) {
            return true;
        }
    }

    return false;
}

std::unique_ptr<aiScene> makeQuadScene()
{
    auto scene = std::make_unique<aiScene>();
    scene->mRootNode = new aiNode();
    scene->mRootNode->mName = aiString("Root");

    scene->mNumMaterials = 1;
    scene->mMaterials = new aiMaterial*[1];
    scene->mMaterials[0] = new aiMaterial();

    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh*[1];
    scene->mMeshes[0] = new aiMesh();

    aiMesh* mesh = scene->mMeshes[0];
    mesh->mPrimitiveTypes = aiPrimitiveType_POLYGON;
    mesh->mMaterialIndex = 0;
    mesh->mNumVertices = 4;
    mesh->mVertices = new aiVector3D[4]{
        aiVector3D(0.0f, 0.0f, 2.0f),
        aiVector3D(1.0f, 0.0f, 2.0f),
        aiVector3D(1.0f, 1.0f, 2.0f),
        aiVector3D(0.0f, 1.0f, 2.0f),
    };
    mesh->mNormals = new aiVector3D[4]{
        aiVector3D(0.0f, 0.0f, 1.0f),
        aiVector3D(0.0f, 0.0f, 1.0f),
        aiVector3D(0.0f, 0.0f, 1.0f),
        aiVector3D(0.0f, 0.0f, 1.0f),
    };
    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 4;
    mesh->mFaces[0].mIndices = new unsigned int[4]{0, 1, 2, 3};

    scene->mRootNode->mNumChildren = 2;
    scene->mRootNode->mChildren = new aiNode*[2];
    scene->mRootNode->mChildren[0] = new aiNode();
    scene->mRootNode->mChildren[1] = new aiNode();

    scene->mRootNode->mChildren[0]->mParent = scene->mRootNode;
    scene->mRootNode->mChildren[0]->mName = aiString("IdentityInstance");
    scene->mRootNode->mChildren[0]->mNumMeshes = 1;
    scene->mRootNode->mChildren[0]->mMeshes = new unsigned int[1]{0};

    aiMatrix4x4 rotatedInstance;
    aiMatrix4x4::RotationX(static_cast<float>(AI_MATH_PI) * 0.5f, rotatedInstance);
    scene->mRootNode->mChildren[1]->mParent = scene->mRootNode;
    scene->mRootNode->mChildren[1]->mName = aiString("RotatedInstance");
    scene->mRootNode->mChildren[1]->mTransformation = rotatedInstance;
    scene->mRootNode->mChildren[1]->mNumMeshes = 1;
    scene->mRootNode->mChildren[1]->mMeshes = new unsigned int[1]{0};

    return scene;
}

void exportScene(const aiScene& scene,
                 std::string_view formatId,
                 const std::filesystem::path& outputPath)
{
    Assimp::Exporter exporter;
    REQUIRE(exporter.Export(&scene, formatId.data(), outputPath.string()) == aiReturn_SUCCESS);
}

Eigen::Vector3f transformPosition(const aiMatrix4x4& transform, const Eigen::Vector3f& input)
{
    const aiVector3D transformed = transform * aiVector3D(input.x(), input.y(), input.z());
    return {transformed.x, transformed.y, transformed.z};
}

Eigen::Vector3f transformNormal(const aiMatrix4x4& transform, const Eigen::Vector3f& input)
{
    aiMatrix3x3 normalTransform(transform);
    normalTransform.Inverse().Transpose();

    aiVector3D transformed = normalTransform * aiVector3D(input.x(), input.y(), input.z());
    transformed.Normalize();
    return {transformed.x, transformed.y, transformed.z};
}

bool approximatelyEqual(const Eigen::Vector3f& left, const Eigen::Vector3f& right, float epsilon = 1.0e-4f)
{
    return (left - right).cwiseAbs().maxCoeff() <= epsilon;
}

bool containsVector(const std::vector<Eigen::Vector3f>& values, const Eigen::Vector3f& target)
{
    for (const auto& value : values) {
        if (approximatelyEqual(value, target)) {
            return true;
        }
    }

    return false;
}

int countVector(const std::vector<Eigen::Vector3f>& values, const Eigen::Vector3f& target)
{
    int count = 0;
    for (const auto& value : values) {
        if (approximatelyEqual(value, target)) {
            ++count;
        }
    }

    return count;
}

} // namespace

TEST_CASE("quad OBJ round trip preserves six quads")
{
    retoprime::MeshIO io;
    const auto loaded = io.load(fixture("cube-quads.obj"));

    REQUIRE(loaded.mesh.faces.size() == 6);
    CHECK(loaded.mesh.validate().quadCount == 6);

    const auto output = tempPath("cube-roundtrip.obj");
    io.save(loaded.mesh, output);

    CHECK(io.load(output).mesh.validate().quadCount == 6);
}

TEST_CASE("quad OBJ round trip supports a Unicode native path")
{
    retoprime::MeshIO io;
    const auto loaded = io.load(fixture("cube-quads.obj"));
    const auto directory = tempPath("unicode-workspace");
    std::filesystem::create_directories(directory);
    const auto output = directory / nativePath(QStringLiteral("网格-é.obj"));

    io.save(loaded.mesh, output);
    CHECK(io.load(output).mesh.validate().quadCount == 6);

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

TEST_CASE("unsupported extension is rejected without changing state")
{
    retoprime::MeshIO io;

    CHECK_THROWS_WITH(
        io.load(std::filesystem::path("mesh.stl")),
        Catch::Matchers::ContainsSubstring("FBX or OBJ"));
}

TEST_CASE("FBX save and load path preserves quad faces when the exporter is available")
{
    if (!hasExportExtension("fbx")) {
        SKIP("Assimp FBX exporter is not available in this build.");
    }

    retoprime::Mesh mesh;
    mesh.positions = {
        {-1.0f, -1.0f, 0.0f},
        {1.0f, -1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f},
    };
    mesh.normals = {
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
    };
    mesh.faces = {{0, 1, 2, 3}};

    retoprime::MeshIO io;
    const auto output = tempPath("quad-path.fbx");
    io.save(mesh, output);

    const auto loaded = io.load(output);
    CHECK(loaded.mesh.validate().quadCount == 1);
    CHECK(loaded.mesh.faces.size() == 1);
}

TEST_CASE("FBX import applies node transforms and keeps mesh instances")
{
    if (!hasExportExtension("fbx")) {
        SKIP("Assimp FBX exporter is not available in this build.");
    }

    const auto output = tempPath("transformed-instances.fbx");
    const auto scene = makeQuadScene();
    exportScene(*scene, "fbx", output);

    retoprime::MeshIO io;
    const auto loaded = io.load(output);

    REQUIRE(loaded.mesh.faces.size() == 2);
    CHECK(loaded.mesh.validate().quadCount == 2);
    REQUIRE(loaded.mesh.positions.size() == 8);
    REQUIRE(loaded.mesh.normals.size() == 8);

    const std::array<Eigen::Vector3f, 4> sourcePositions = {
        Eigen::Vector3f{0.0f, 0.0f, 2.0f},
        Eigen::Vector3f{1.0f, 0.0f, 2.0f},
        Eigen::Vector3f{1.0f, 1.0f, 2.0f},
        Eigen::Vector3f{0.0f, 1.0f, 2.0f},
    };
    const Eigen::Vector3f sourceNormal{0.0f, 0.0f, 1.0f};

    for (const auto& position : sourcePositions) {
        CHECK(containsVector(loaded.mesh.positions, position));
        CHECK(containsVector(
            loaded.mesh.positions,
            transformPosition(scene->mRootNode->mChildren[1]->mTransformation, position)));
    }

    CHECK(countVector(loaded.mesh.normals, sourceNormal) == 4);
    CHECK(countVector(
              loaded.mesh.normals,
              transformNormal(scene->mRootNode->mChildren[1]->mTransformation, sourceNormal)) == 4);
}

TEST_CASE("OBJ import skips point and line primitives when polygon geometry is present")
{
    const auto output = tempPath("mixed-primitives.obj");
    std::ofstream stream(output);
    REQUIRE(stream.is_open());
    stream << "o Mixed\n";
    stream << "v 0 0 0\n";
    stream << "v 1 0 0\n";
    stream << "v 1 1 0\n";
    stream << "v 0 1 0\n";
    stream << "v 3 0 0\n";
    stream << "v 4 0 0\n";
    stream << "v 5 0 0\n";
    stream << "p 5\n";
    stream << "l 6 7\n";
    stream << "f 1 2 3 4\n";
    stream.close();

    retoprime::MeshIO io;
    const auto loaded = io.load(output);

    REQUIRE(loaded.mesh.faces.size() == 1);
    CHECK(loaded.mesh.validate().quadCount == 1);
}

TEST_CASE("OBJ import reports empty geometry when only point and line primitives remain")
{
    const auto output = tempPath("non-polygon-primitives.obj");
    std::ofstream stream(output);
    REQUIRE(stream.is_open());
    stream << "o NonPolygon\n";
    stream << "v 0 0 0\n";
    stream << "v 1 0 0\n";
    stream << "v 2 0 0\n";
    stream << "p 1\n";
    stream << "l 2 3\n";
    stream.close();

    retoprime::MeshIO io;

    CHECK_THROWS_WITH(
        io.load(output),
        Catch::Matchers::ContainsSubstring("The file does not contain mesh geometry."));
}

TEST_CASE("validate reports missing geometry")
{
    const retoprime::Mesh mesh;

    CHECK(mesh.validate().errors == QStringList{
                                     QStringLiteral("The file does not contain mesh geometry.")});
}

TEST_CASE("validate reports a face with fewer than three vertices")
{
    retoprime::Mesh mesh;
    mesh.positions = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    mesh.faces = {{0, 1}};

    CHECK(mesh.validate().errors == QStringList{
                                     QStringLiteral("A face has fewer than three vertices.")});
}

TEST_CASE("validate reports a face that references a missing vertex")
{
    retoprime::Mesh mesh;
    mesh.positions = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    mesh.faces = {{0, 1, 3}};

    CHECK(mesh.validate().errors == QStringList{
                                     QStringLiteral("A face references a missing vertex.")});
}

TEST_CASE("safePreprocess removes duplicate indices degenerates and unreferenced vertices")
{
    retoprime::Mesh mesh;
    mesh.positions = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {5.0f, 5.0f, 5.0f},
        {2.0f, 0.0f, 0.0f},
    };
    mesh.faces = {
        {0, 1, 1, 2, 3},
        {0, 0, 2},
        {0, 1, 5},
        {0, 1, 2},
    };

    const auto processed = mesh.safePreprocess();

    CHECK(mesh.positions.size() == 6);
    CHECK(mesh.faces.size() == 4);
    REQUIRE(processed.positions.size() == 4);
    REQUIRE(processed.faces.size() == 2);
    CHECK(processed.faces[0] == std::vector<std::uint32_t>{0, 1, 2, 3});
    CHECK(processed.faces[1] == std::vector<std::uint32_t>{0, 1, 2});
}

TEST_CASE("save rejects meshes with invalid indices")
{
    retoprime::MeshIO io;
    retoprime::Mesh mesh;
    mesh.positions = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    mesh.faces = {{0, 1, 3}};

    CHECK_THROWS_WITH(
        io.save(mesh, tempPath("invalid.obj")),
        Catch::Matchers::ContainsSubstring("A face references a missing vertex."));
}
