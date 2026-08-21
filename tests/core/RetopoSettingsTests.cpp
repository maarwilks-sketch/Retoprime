#include "core/RetopoSettings.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void checkSettings(const retoprime::RetopoSettings& actual,
                   const retoprime::RetopoSettings& expected)
{
    CHECK(actual.targetFaces == expected.targetFaces);
    CHECK(actual.topologyDensity == expected.topologyDensity);
    CHECK(actual.minimumFaceSize == expected.minimumFaceSize);
    CHECK(actual.maximumFaceSize == expected.maximumFaceSize);
    CHECK(actual.adaptiveDetail == expected.adaptiveDetail);
    CHECK(actual.projectionStrength == expected.projectionStrength);
    CHECK(actual.smoothIterations == expected.smoothIterations);
    CHECK(actual.preserveFeatures == expected.preserveFeatures);
    CHECK(actual.sharpEdgeDegrees == expected.sharpEdgeDegrees);
    CHECK(actual.quadRegularity == expected.quadRegularity);
    CHECK(actual.symmetryTolerance == expected.symmetryTolerance);
    CHECK(actual.symmetryAxis == expected.symmetryAxis);
}

} // namespace

TEST_CASE("default settings match the documented character baseline")
{
    const retoprime::RetopoSettings settings;

    CHECK(settings.targetFaces == 25000);
    CHECK(settings.topologyDensity == 0.5f);
    CHECK(settings.minimumFaceSize == 0.05f);
    CHECK(settings.maximumFaceSize == 1.0f);
    CHECK(settings.adaptiveDetail == 0.5f);
    CHECK(settings.projectionStrength == 1.0f);
    CHECK(settings.smoothIterations == 2);
    CHECK(settings.preserveFeatures == 0.75f);
    CHECK(settings.sharpEdgeDegrees == 60.0f);
    CHECK(settings.quadRegularity == 1.0f);
    CHECK(settings.symmetryTolerance == 0.001f);
    CHECK(settings.symmetryAxis == retoprime::SymmetryAxis::Off);
    CHECK(settings.validate().empty());
}

TEST_CASE("fixed presets expand to the expected settings payload")
{
    checkSettings(
        retoprime::settingsForPreset(retoprime::RetopoPreset::GameProp),
        retoprime::RetopoSettings{
            5000, 0.35f, 0.10f, 1.0f, 0.35f, 0.85f, 1, 0.55f, 45.0f, 0.80f, 0.002f,
            retoprime::SymmetryAxis::Off});

    checkSettings(
        retoprime::settingsForPreset(retoprime::RetopoPreset::Character),
        retoprime::RetopoSettings{});

    checkSettings(
        retoprime::settingsForPreset(retoprime::RetopoPreset::HeroAsset),
        retoprime::RetopoSettings{
            75000, 0.65f, 0.03f, 0.80f, 0.70f, 1.0f, 3, 0.85f, 70.0f, 1.0f, 0.0005f,
            retoprime::SymmetryAxis::Off});

    checkSettings(
        retoprime::settingsForPreset(retoprime::RetopoPreset::FilmHighDetail),
        retoprime::RetopoSettings{
            250000, 0.85f, 0.01f, 0.60f, 0.90f, 1.0f, 4, 0.95f, 80.0f, 0.95f, 0.00025f,
            retoprime::SymmetryAxis::Off});
}

TEST_CASE("custom preset has no fixed settings payload")
{
    CHECK_THROWS_AS(
        retoprime::settingsForPreset(retoprime::RetopoPreset::Custom),
        std::invalid_argument);
    CHECK_THROWS_WITH(
        retoprime::settingsForPreset(retoprime::RetopoPreset::Custom),
        "RetopoPreset::Custom has no fixed settings; preserve the current values instead.");
}

TEST_CASE("unknown preset values are rejected")
{
    CHECK_THROWS_AS(
        retoprime::settingsForPreset(static_cast<retoprime::RetopoPreset>(999)),
        std::invalid_argument);
    CHECK_THROWS_WITH(
        retoprime::settingsForPreset(static_cast<retoprime::RetopoPreset>(999)),
        "Unknown retopology preset.");
}

TEST_CASE("target polygon count range is enforced at both boundaries")
{
    retoprime::RetopoSettings tooLow;
    tooLow.targetFaces = 499;
    CHECK(tooLow.validate() == std::vector<QString>{
                                  QStringLiteral(
                                      "Target polygon count must be between 500 and 2,000,000.")});

    retoprime::RetopoSettings tooHigh;
    tooHigh.targetFaces = 2000001;
    CHECK(tooHigh.validate() == std::vector<QString>{
                                   QStringLiteral(
                                       "Target polygon count must be between 500 and 2,000,000.")});
}

TEST_CASE("normalised sliders enforce the closed unit interval")
{
    retoprime::RetopoSettings settings;
    settings.topologyDensity = -0.01f;
    settings.adaptiveDetail = 1.01f;
    settings.projectionStrength = -0.01f;
    settings.preserveFeatures = 1.01f;
    settings.quadRegularity = -0.01f;
    settings.symmetryTolerance = 1.01f;

    CHECK(settings.validate() == std::vector<QString>{
                                   QStringLiteral(
                                       "Overall topology density must be between 0.0 and 1.0."),
                                   QStringLiteral("Adaptive detail must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Surface projection strength must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Feature preservation must be between 0.0 and 1.0."),
                                   QStringLiteral("Quad regularity must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Symmetry tolerance must be between 0.0 and 1.0.")});
}

TEST_CASE("non-finite unit-interval and sharp-edge fields are rejected in stable order")
{
    retoprime::RetopoSettings settings;
    settings.topologyDensity = std::numeric_limits<float>::quiet_NaN();
    settings.adaptiveDetail = std::numeric_limits<float>::infinity();
    settings.projectionStrength = -std::numeric_limits<float>::infinity();
    settings.preserveFeatures = std::numeric_limits<float>::quiet_NaN();
    settings.sharpEdgeDegrees = std::numeric_limits<float>::quiet_NaN();
    settings.quadRegularity = std::numeric_limits<float>::infinity();
    settings.symmetryTolerance = -std::numeric_limits<float>::infinity();

    CHECK(settings.validate() == std::vector<QString>{
                                   QStringLiteral(
                                       "Overall topology density must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Adaptive detail must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Surface projection strength must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Feature preservation must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Sharp-edge sensitivity must be between 0.0 and 180.0 degrees."),
                                   QStringLiteral(
                                       "Quad regularity must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Symmetry tolerance must be between 0.0 and 1.0.")});
}

TEST_CASE("face-size limits require positive values and a valid ordering")
{
    retoprime::RetopoSettings settings;
    settings.minimumFaceSize = 0.0f;
    settings.maximumFaceSize = -0.5f;

    CHECK(settings.validate() == std::vector<QString>{
                                   QStringLiteral("Minimum face size must be greater than 0.0."),
                                   QStringLiteral("Maximum face size must be greater than 0.0."),
                                   QStringLiteral(
                                       "Minimum face size cannot exceed maximum face size.")});
}

TEST_CASE("non-finite face sizes are rejected without falling back to preset defaults")
{
    retoprime::RetopoSettings settings;
    settings.minimumFaceSize = std::numeric_limits<float>::infinity();
    settings.maximumFaceSize = std::numeric_limits<float>::quiet_NaN();

    CHECK(settings.validate() == std::vector<QString>{
                                   QStringLiteral("Minimum face size must be greater than 0.0."),
                                   QStringLiteral("Maximum face size must be greater than 0.0.")});
}

TEST_CASE("smoothness and sharp-edge sensitivity ranges are enforced")
{
    retoprime::RetopoSettings settings;
    settings.smoothIterations = 11;
    settings.sharpEdgeDegrees = 180.1f;

    CHECK(settings.validate() == std::vector<QString>{
                                   QStringLiteral("Smooth iterations must be between 0 and 10."),
                                   QStringLiteral(
                                       "Sharp-edge sensitivity must be between 0.0 and 180.0 degrees.")});
}

TEST_CASE("validation reports multiple errors in a stable field order")
{
    retoprime::RetopoSettings settings;
    settings.targetFaces = 0;
    settings.topologyDensity = 1.5f;
    settings.minimumFaceSize = -0.1f;
    settings.maximumFaceSize = -0.2f;
    settings.adaptiveDetail = -0.2f;
    settings.projectionStrength = 1.5f;
    settings.smoothIterations = -1;
    settings.preserveFeatures = 1.2f;
    settings.sharpEdgeDegrees = -1.0f;
    settings.quadRegularity = 1.1f;
    settings.symmetryTolerance = -0.001f;

    CHECK(settings.validate() == std::vector<QString>{
                                   QStringLiteral(
                                       "Target polygon count must be between 500 and 2,000,000."),
                                   QStringLiteral(
                                       "Overall topology density must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Minimum face size must be greater than 0.0."),
                                   QStringLiteral(
                                       "Maximum face size must be greater than 0.0."),
                                   QStringLiteral(
                                       "Minimum face size cannot exceed maximum face size."),
                                   QStringLiteral(
                                       "Adaptive detail must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Surface projection strength must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Smooth iterations must be between 0 and 10."),
                                   QStringLiteral(
                                       "Feature preservation must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Sharp-edge sensitivity must be between 0.0 and 180.0 degrees."),
                                   QStringLiteral(
                                       "Quad regularity must be between 0.0 and 1.0."),
                                   QStringLiteral(
                                       "Symmetry tolerance must be between 0.0 and 1.0.")});
}
