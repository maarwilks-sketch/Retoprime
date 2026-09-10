#pragma once

#include "core/Mesh.h"

#include <QStringList>

#include <filesystem>

namespace retoprime {

struct MeshLoadResult {
    Mesh mesh;
    QStringList warnings;
};

class MeshIO {
public:
    [[nodiscard]] MeshLoadResult load(const std::filesystem::path& path) const;
    void save(const Mesh& mesh, const std::filesystem::path& path) const;
};

} // namespace retoprime
