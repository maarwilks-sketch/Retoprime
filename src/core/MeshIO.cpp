#include "core/MeshIO.h"

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <QString>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

namespace retoprime {

namespace {

constexpr unsigned int kAssimpLoadFlags =
    aiProcess_JoinIdenticalVertices |
    aiProcess_GenSmoothNormals |
    aiProcess_ValidateDataStructure |
    aiProcess_SortByPType;

QString unsupportedExtensionMessage()
{
    return QStringLiteral("Only FBX or OBJ files are supported.");
}

std::string toStdString(const QStringList& messages)
{
    return messages.join(QStringLiteral(" ")).toStdString();
}

std::string normalizedExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

std::string assimpPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
#else
    return path.string();
#endif
}

void validateExtension(const std::filesystem::path& path)
{
    const auto extension = normalizedExtension(path);
    if (extension != ".fbx" && extension != ".obj") {
        throw std::runtime_error(unsupportedExtensionMessage().toStdString());
    }
}

Mesh convertScene(const aiScene& scene)
{
    Mesh mesh;

    const auto isPolygonMesh = [](const aiMesh& sourceMesh) {
        if (sourceMesh.mPrimitiveTypes != 0) {
            return (sourceMesh.mPrimitiveTypes & (aiPrimitiveType_TRIANGLE | aiPrimitiveType_POLYGON)) != 0;
        }

        for (unsigned int faceIndex = 0; faceIndex < sourceMesh.mNumFaces; ++faceIndex) {
            if (sourceMesh.mFaces[faceIndex].mNumIndices >= 3) {
                return true;
            }
        }

        return false;
    };

    std::size_t totalVertices = 0;
    std::size_t totalFaces = 0;
    const auto countNodeGeometry = [&](const auto& self, const aiNode& node) -> void {
        for (unsigned int meshRefIndex = 0; meshRefIndex < node.mNumMeshes; ++meshRefIndex) {
            const unsigned int meshIndex = node.mMeshes[meshRefIndex];
            if (meshIndex >= scene.mNumMeshes || scene.mMeshes[meshIndex] == nullptr) {
                continue;
            }

            const aiMesh& sourceMesh = *scene.mMeshes[meshIndex];
            if (!isPolygonMesh(sourceMesh)) {
                continue;
            }

            totalVertices += sourceMesh.mNumVertices;
            for (unsigned int faceIndex = 0; faceIndex < sourceMesh.mNumFaces; ++faceIndex) {
                if (sourceMesh.mFaces[faceIndex].mNumIndices >= 3) {
                    ++totalFaces;
                }
            }
        }

        for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex) {
            if (node.mChildren[childIndex] != nullptr) {
                self(self, *node.mChildren[childIndex]);
            }
        }
    };

    if (scene.mRootNode != nullptr) {
        countNodeGeometry(countNodeGeometry, *scene.mRootNode);
    }

    mesh.positions.reserve(totalVertices);
    mesh.normals.reserve(totalVertices);
    mesh.faces.reserve(totalFaces);

    const auto appendMeshInstance = [&](const aiMesh& sourceMesh, const aiMatrix4x4& transform) {
        if (!isPolygonMesh(sourceMesh)) {
            return;
        }

        const aiMatrix3x3 linearTransform(transform);
        aiMatrix3x3 normalTransform = linearTransform;
        const float determinant = linearTransform.Determinant();
        const bool flipsOrientation = determinant < 0.0f;
        if (sourceMesh.HasNormals() && determinant != 0.0f) {
            normalTransform.Inverse().Transpose();
        }

        const auto vertexOffset = static_cast<std::uint32_t>(mesh.positions.size());
        for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh.mNumVertices; ++vertexIndex) {
            const aiVector3D transformedPosition = transform * sourceMesh.mVertices[vertexIndex];
            mesh.positions.emplace_back(
                transformedPosition.x, transformedPosition.y, transformedPosition.z);

            if (sourceMesh.HasNormals()) {
                aiVector3D transformedNormal = normalTransform * sourceMesh.mNormals[vertexIndex];
                transformedNormal.Normalize();
                mesh.normals.emplace_back(
                    transformedNormal.x, transformedNormal.y, transformedNormal.z);
            } else {
                mesh.normals.emplace_back(Eigen::Vector3f::Zero());
            }
        }

        for (unsigned int faceIndex = 0; faceIndex < sourceMesh.mNumFaces; ++faceIndex) {
            const aiFace& sourceFace = sourceMesh.mFaces[faceIndex];
            if (sourceFace.mNumIndices < 3) {
                continue;
            }

            auto& face = mesh.faces.emplace_back();
            face.reserve(sourceFace.mNumIndices);

            if (flipsOrientation) {
                for (unsigned int index = sourceFace.mNumIndices; index-- > 0;) {
                    face.push_back(vertexOffset + sourceFace.mIndices[index]);
                }
            } else {
                for (unsigned int index = 0; index < sourceFace.mNumIndices; ++index) {
                    face.push_back(vertexOffset + sourceFace.mIndices[index]);
                }
            }
        }
    };

    const auto visitNode = [&](const auto& self, const aiNode& node, const aiMatrix4x4& parentTransform) -> void {
        const aiMatrix4x4 accumulatedTransform = parentTransform * node.mTransformation;

        for (unsigned int meshRefIndex = 0; meshRefIndex < node.mNumMeshes; ++meshRefIndex) {
            const unsigned int meshIndex = node.mMeshes[meshRefIndex];
            if (meshIndex >= scene.mNumMeshes || scene.mMeshes[meshIndex] == nullptr) {
                continue;
            }

            appendMeshInstance(*scene.mMeshes[meshIndex], accumulatedTransform);
        }

        for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex) {
            if (node.mChildren[childIndex] != nullptr) {
                self(self, *node.mChildren[childIndex], accumulatedTransform);
            }
        }
    };

    if (scene.mRootNode != nullptr) {
        visitNode(visitNode, *scene.mRootNode, aiMatrix4x4());
    }

    return mesh;
}

std::unique_ptr<aiScene> makeScene(const Mesh& mesh)
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

    aiMesh* outputMesh = scene->mMeshes[0];
    outputMesh->mName = aiString("RETOPRIME Mesh");
    outputMesh->mMaterialIndex = 0;
    outputMesh->mNumVertices = static_cast<unsigned int>(mesh.positions.size());
    outputMesh->mVertices = new aiVector3D[outputMesh->mNumVertices];

    for (unsigned int index = 0; index < outputMesh->mNumVertices; ++index) {
        const auto& position = mesh.positions[index];
        outputMesh->mVertices[index] = aiVector3D(position.x(), position.y(), position.z());
    }

    if (mesh.normals.size() == mesh.positions.size()) {
        outputMesh->mNormals = new aiVector3D[outputMesh->mNumVertices];
        for (unsigned int index = 0; index < outputMesh->mNumVertices; ++index) {
            const auto& normal = mesh.normals[index];
            outputMesh->mNormals[index] = aiVector3D(normal.x(), normal.y(), normal.z());
        }
    }

    outputMesh->mNumFaces = static_cast<unsigned int>(mesh.faces.size());
    outputMesh->mFaces = new aiFace[outputMesh->mNumFaces];

    for (unsigned int faceIndex = 0; faceIndex < outputMesh->mNumFaces; ++faceIndex) {
        const auto& sourceFace = mesh.faces[faceIndex];
        aiFace& outputFace = outputMesh->mFaces[faceIndex];
        outputFace.mNumIndices = static_cast<unsigned int>(sourceFace.size());
        outputFace.mIndices = new unsigned int[outputFace.mNumIndices];

        for (unsigned int index = 0; index < outputFace.mNumIndices; ++index) {
            outputFace.mIndices[index] = sourceFace[index];
        }
    }

    scene->mRootNode->mNumMeshes = 1;
    scene->mRootNode->mMeshes = new unsigned int[1];
    scene->mRootNode->mMeshes[0] = 0;

    return scene;
}

const char* exportFormatIdForPath(const std::filesystem::path& path)
{
    const auto extension = normalizedExtension(path);
    if (extension == ".fbx") {
        return "fbx";
    }

    if (extension == ".obj") {
        return "objnomtl";
    }

    throw std::runtime_error(unsupportedExtensionMessage().toStdString());
}

} // namespace

MeshLoadResult MeshIO::load(const std::filesystem::path& path) const
{
    validateExtension(path);

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(assimpPath(path), kAssimpLoadFlags);
    if (scene == nullptr) {
        throw std::runtime_error(importer.GetErrorString());
    }

    MeshLoadResult result;
    result.mesh = convertScene(*scene);

    const auto validation = result.mesh.validate();
    if (!validation.errors.isEmpty()) {
        throw std::runtime_error(toStdString(validation.errors));
    }

    if (scene->mNumMeshes > 1) {
        result.warnings.push_back(QStringLiteral("Imported scene meshes were combined."));
    }

    return result;
}

void MeshIO::save(const Mesh& mesh, const std::filesystem::path& path) const
{
    validateExtension(path);

    const auto validation = mesh.validate();
    if (!validation.errors.isEmpty()) {
        throw std::runtime_error(toStdString(validation.errors));
    }

    auto scene = makeScene(mesh);

    Assimp::Exporter exporter;
    const aiReturn status = exporter.Export(
        scene.get(), exportFormatIdForPath(path), assimpPath(path));
    if (status != aiReturn_SUCCESS) {
        throw std::runtime_error(exporter.GetErrorString());
    }
}

} // namespace retoprime
