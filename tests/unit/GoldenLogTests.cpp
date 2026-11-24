// SignalFlux — golden event logs: the cross-platform determinism gate (D25).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Every built-in example is rendered for a fixed length and compared byte for
// byte with a committed log in tests/fixtures/golden/. CI runs this on macOS,
// Windows, and Linux, so any platform-dependent behavior fails the build.
//
// To regenerate after an intended behavior change, set SIGNALFLUX_UPDATE_GOLDEN=1
// and run the tests, then review the diff before committing it.

#include "domain/Examples.h"
#include "engine/EventLog.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using namespace signalflux;

namespace {

constexpr std::int64_t kGoldenBeats = 32;

std::string goldenPath(std::string_view name) {
    return std::string(SIGNALFLUX_GOLDEN_DIR) + "/" + std::string(name) + ".log";
}

std::string readBinary(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool updateRequested() {
    const char* v = std::getenv("SIGNALFLUX_UPDATE_GOLDEN");
    return v != nullptr && std::string_view(v) == "1";
}

/// First differing line, for a readable failure message.
std::string firstDifference(const std::string& expected, const std::string& actual) {
    std::istringstream e(expected), a(actual);
    std::string le, la;
    for (int line = 1;; ++line) {
        const bool he = static_cast<bool>(std::getline(e, le));
        const bool ha = static_cast<bool>(std::getline(a, la));
        if (!he && !ha) return "no line difference (check line endings)";
        if (le != la || he != ha)
            return "line " + std::to_string(line) + ": expected '" + (he ? le : "<eof>") + "' got '" + (ha ? la : "<eof>") + "'";
    }
}

} // namespace

TEST_CASE("Golden logs match for every built-in example", "[engine][golden][determinism]") {
    for (const auto name : examples::names()) {
        INFO("example: " << name);
        const auto doc = examples::make(name);
        REQUIRE(doc);
        const auto actual = engine::renderCanonicalLog(*doc, MusicalTick{kGoldenBeats * kTicksPerQuarter});
        REQUIRE_FALSE(actual.empty());

        const auto path = goldenPath(name);
        if (updateRequested()) {
            std::ofstream(path, std::ios::binary) << actual;
            WARN("updated " << path);
            continue;
        }

        const auto expected = readBinary(path);
        INFO("golden file: " << path);
        REQUIRE_FALSE(expected.empty());
        INFO(firstDifference(expected, actual));
        CHECK(actual == expected);
    }
}

TEST_CASE("Rendering is repeatable within one process", "[engine][golden][determinism]") {
    for (const auto name : examples::names()) {
        const auto doc = *examples::make(name);
        const MusicalTick len{16 * kTicksPerQuarter};
        CHECK(engine::renderCanonicalLog(doc, len) == engine::renderCanonicalLog(doc, len));
    }
}
