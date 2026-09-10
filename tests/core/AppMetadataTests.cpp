#include <QVersionNumber>
#include <QString>

#include <catch2/catch_test_macros.hpp>

namespace retoprime {
QString applicationName();
QVersionNumber applicationVersion();
}

TEST_CASE("application has RETOPRIME identity") {
    CHECK(retoprime::applicationName() == QStringLiteral("RETOPRIME"));
    CHECK(retoprime::applicationVersion() == QVersionNumber(1, 0, 0));
}
