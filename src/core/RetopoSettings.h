#pragma once

#include <QString>

#include <stdexcept>
#include <vector>

namespace retoprime {

enum class SymmetryAxis { Off, X, Y, Z };

enum class RetopoPreset { GameProp, Character, HeroAsset, FilmHighDetail, Custom };

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

    [[nodiscard]] std::vector<QString> validate() const;
};

// Custom has no fixed payload. UI callers should preserve the current values
// instead of calling settingsForPreset(RetopoPreset::Custom).
[[nodiscard]] RetopoSettings settingsForPreset(RetopoPreset preset);

} // namespace retoprime
