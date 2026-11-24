// SignalFlux — project files (task SF-010; DECISIONS D17, D25).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Format: UTF-8 JSON, extension .sflux, top-level "format": "signalflux-project"
// and integer "schemaVersion". Writing is canonical: object keys sorted, arrays
// in document order, integers only, '\n' line endings, so the same document
// produces identical bytes on every platform (D25). 64-bit seeds are written as
// decimal strings so other JSON tools cannot round them.
//
// Loading never mutates anything on failure: it returns either a complete,
// validated document or errors. Older schema versions are upgraded by the
// MigrationRegistry; newer ones are refused with a clear message.
//
// Serialization works on domain data only, never UI component state.

#pragma once

#include "domain/Project.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace signalflux::persistence {

inline constexpr const char* kFormatName = "signalflux-project";
inline constexpr const char* kFileExtension = ".sflux";

/// Canonical JSON text for a document. Always ends with '\n'.
[[nodiscard]] std::string toJson(const ProjectDocument& doc);

struct LoadResult {
    std::optional<ProjectDocument> document;   ///< Set only on success.
    std::vector<std::string> errors;           ///< Why loading failed.
    std::vector<std::string> warnings;         ///< Loaded, but something was ignored or upgraded.
    std::uint32_t fileSchemaVersion = 0;

    [[nodiscard]] bool ok() const noexcept { return document.has_value(); }
};

class MigrationRegistry;

/// Parses, migrates, and validates. Never returns a partially-filled document.
[[nodiscard]] LoadResult fromJson(std::string_view text);
[[nodiscard]] LoadResult fromJson(std::string_view text, const MigrationRegistry& migrations);

/// Atomic save (D17): writes a temporary file beside the target, flushes it,
/// then renames it over the target. Returns an error message on failure.
[[nodiscard]] std::optional<std::string> saveToFile(const ProjectDocument& doc, const std::filesystem::path& path);

[[nodiscard]] LoadResult loadFromFile(const std::filesystem::path& path);

/// Upgrades raw JSON text of older schema versions, one version at a time.
/// Migration N turns a version-N file into a version-(N+1) file. Immutable
/// once built; no global mutable state (AGENTS.md).
class MigrationRegistry {
public:
    using Migration = std::function<std::string(const std::string& json)>;

    /// The migrations shipped with this build (none yet: schema 1 is the first).
    static const MigrationRegistry& builtIn();

    MigrationRegistry& add(std::uint32_t fromVersion, Migration m) {
        migrations_[fromVersion] = std::move(m);
        return *this;
    }
    [[nodiscard]] bool has(std::uint32_t fromVersion) const { return migrations_.count(fromVersion) != 0; }
    [[nodiscard]] std::string run(std::uint32_t fromVersion, const std::string& json) const {
        return migrations_.at(fromVersion)(json);
    }

private:
    std::map<std::uint32_t, Migration> migrations_;
};

} // namespace signalflux::persistence
