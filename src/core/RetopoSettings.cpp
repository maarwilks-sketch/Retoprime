#include "core/RetopoSettings.h"

#include <cmath>
#include <stdexcept>

namespace retoprime {

namespace {

bool isFinite(const float value)
{
    return std::isfinite(value);
}

void appendUnitIntervalError(std::vector<QString>& errors,
                             float value,
                             const char* message)
{
    if (!isFinite(value) || value < 0.0f || value > 1.0f) {
        errors.emplace_back(QString::fromUtf8(message));
    }
}

RetopoSettings gamePropSettings()
{
    return {
        5000,
        0.35f,
        0.10f,
        1.0f,
        0.35f,
        0.85f,
        1,
        0.55f,
        45.0f,
        0.80f,
        0.002f,
        SymmetryAxis::Off,
    };
}

RetopoSettings heroAssetSettings()
{
    return {
        75000,
        0.65f,
        0.03f,
        0.80f,
        0.70f,
        1.0f,
        3,
        0.85f,
        70.0f,
        1.0f,
        0.0005f,
        SymmetryAxis::Off,
    };
}

RetopoSettings filmHighDetailSettings()
{
    return {
        250000,
        0.85f,
        0.01f,
        0.60f,
        0.90f,
        1.0f,
        4,
        0.95f,
        80.0f,
        0.95f,
        0.00025f,
        SymmetryAxis::Off,
    };
}

} // namespace

std::vector<QString> RetopoSettings::validate() const
{
    std::vector<QString> errors;

    if (targetFaces < 500 || targetFaces > 2000000) {
        errors.emplace_back(QStringLiteral("Target polygon count must be between 500 and 2,000,000."));
    }

    appendUnitIntervalError(
        errors,
        topologyDensity,
        "Overall topology density must be between 0.0 and 1.0.");

    if (!isFinite(minimumFaceSize) || minimumFaceSize <= 0.0f) {
        errors.emplace_back(QStringLiteral("Minimum face size must be greater than 0.0."));
    }

    if (!isFinite(maximumFaceSize) || maximumFaceSize <= 0.0f) {
        errors.emplace_back(QStringLiteral("Maximum face size must be greater than 0.0."));
    }

    if (isFinite(minimumFaceSize) && isFinite(maximumFaceSize) && minimumFaceSize > maximumFaceSize) {
        errors.emplace_back(QStringLiteral("Minimum face size cannot exceed maximum face size."));
    }

    appendUnitIntervalError(errors, adaptiveDetail, "Adaptive detail must be between 0.0 and 1.0.");
    appendUnitIntervalError(
        errors,
        projectionStrength,
        "Surface projection strength must be between 0.0 and 1.0.");

    if (smoothIterations < 0 || smoothIterations > 10) {
        errors.emplace_back(QStringLiteral("Smooth iterations must be between 0 and 10."));
    }

    appendUnitIntervalError(
        errors,
        preserveFeatures,
        "Feature preservation must be between 0.0 and 1.0.");

    if (!isFinite(sharpEdgeDegrees) || sharpEdgeDegrees < 0.0f || sharpEdgeDegrees > 180.0f) {
        errors.emplace_back(
            QStringLiteral("Sharp-edge sensitivity must be between 0.0 and 180.0 degrees."));
    }

    appendUnitIntervalError(errors, quadRegularity, "Quad regularity must be between 0.0 and 1.0.");
    appendUnitIntervalError(
        errors,
        symmetryTolerance,
        "Symmetry tolerance must be between 0.0 and 1.0.");

    return errors;
}

RetopoSettings settingsForPreset(const RetopoPreset preset)
{
    switch (preset) {
    case RetopoPreset::GameProp:
        return gamePropSettings();
    case RetopoPreset::Character:
        return {};
    case RetopoPreset::HeroAsset:
        return heroAssetSettings();
    case RetopoPreset::FilmHighDetail:
        return filmHighDetailSettings();
    case RetopoPreset::Custom:
        throw std::invalid_argument(
            "RetopoPreset::Custom has no fixed settings; preserve the current values instead.");
    }

    throw std::invalid_argument("Unknown retopology preset.");
}

} // namespace retoprime
