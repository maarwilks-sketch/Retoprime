#pragma once

#include <Eigen/Core>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace retoprime {

struct MeshValidation {
    QStringList errors;
    QStringList warnings;
    std::size_t triangleCount = 0;
    std::size_t quadCount = 0;
};

struct Mesh {
    std::vector<Eigen::Vector3f> positions;
    std::vector<Eigen::Vector3f> normals;
    std::vector<std::vector<std::uint32_t>> faces;

    [[nodiscard]] MeshValidation validate() const;
    [[nodiscard]] Mesh safePreprocess() const;
};

} // namespace retoprime
