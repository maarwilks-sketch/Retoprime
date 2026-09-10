#include "core/Mesh.h"

#include <Eigen/Geometry>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace retoprime {

namespace {

constexpr float kZeroAreaEpsilon = 1.0e-12f;

std::vector<std::uint32_t> deduplicateFace(const std::vector<std::uint32_t>& face)
{
    std::vector<std::uint32_t> cleaned;
    cleaned.reserve(face.size());

    std::unordered_set<std::uint32_t> seen;
    for (const auto index : face) {
        if (seen.insert(index).second) {
            cleaned.push_back(index);
        }
    }

    return cleaned;
}

bool hasMissingVertex(const std::vector<std::uint32_t>& face,
                      const std::vector<Eigen::Vector3f>& positions)
{
    return std::any_of(face.begin(), face.end(), [&](const auto index) {
        return index >= positions.size();
    });
}

bool isZeroAreaFace(const std::vector<std::uint32_t>& face,
                    const std::vector<Eigen::Vector3f>& positions)
{
    if (face.size() < 3 || hasMissingVertex(face, positions)) {
        return true;
    }

    const Eigen::Vector3d origin = positions[face.front()].cast<double>();
    Eigen::Vector3d areaVector = Eigen::Vector3d::Zero();

    for (std::size_t index = 1; index + 1 < face.size(); ++index) {
        const Eigen::Vector3d edgeA = positions[face[index]].cast<double>() - origin;
        const Eigen::Vector3d edgeB = positions[face[index + 1]].cast<double>() - origin;
        areaVector += edgeA.cross(edgeB);
    }

    return areaVector.squaredNorm() <= kZeroAreaEpsilon;
}

} // namespace

MeshValidation Mesh::validate() const
{
    MeshValidation result;

    if (positions.empty() || faces.empty()) {
        result.errors.push_back(QStringLiteral("The file does not contain mesh geometry."));
    }

    for (const auto& face : faces) {
        if (face.size() < 3) {
            result.errors.push_back(QStringLiteral("A face has fewer than three vertices."));
            continue;
        }

        if (hasMissingVertex(face, positions)) {
            result.errors.push_back(QStringLiteral("A face references a missing vertex."));
            continue;
        }

        if (face.size() == 3) {
            ++result.triangleCount;
        } else if (face.size() == 4) {
            ++result.quadCount;
        }
    }

    return result;
}

Mesh Mesh::safePreprocess() const
{
    Mesh processed;

    std::vector<std::vector<std::uint32_t>> cleanedFaces;
    cleanedFaces.reserve(faces.size());

    for (const auto& face : faces) {
        auto cleanedFace = deduplicateFace(face);
        if (cleanedFace.size() < 3) {
            continue;
        }

        if (hasMissingVertex(cleanedFace, positions)) {
            continue;
        }

        if (isZeroAreaFace(cleanedFace, positions)) {
            continue;
        }

        cleanedFaces.push_back(std::move(cleanedFace));
    }

    std::vector<bool> referenced(positions.size(), false);
    for (const auto& face : cleanedFaces) {
        for (const auto index : face) {
            referenced[index] = true;
        }
    }

    std::vector<std::uint32_t> remap(positions.size(), 0);
    processed.positions.reserve(positions.size());
    const bool remapNormals = normals.size() == positions.size();
    if (remapNormals) {
        processed.normals.reserve(normals.size());
    }

    for (std::size_t index = 0; index < positions.size(); ++index) {
        if (!referenced[index]) {
            continue;
        }

        remap[index] = static_cast<std::uint32_t>(processed.positions.size());
        processed.positions.push_back(positions[index]);
        if (remapNormals) {
            processed.normals.push_back(normals[index]);
        }
    }

    processed.faces.reserve(cleanedFaces.size());
    for (const auto& face : cleanedFaces) {
        auto& remappedFace = processed.faces.emplace_back();
        remappedFace.reserve(face.size());
        for (const auto index : face) {
            remappedFace.push_back(remap[index]);
        }
    }

    return processed;
}

} // namespace retoprime
