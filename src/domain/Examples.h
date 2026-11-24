// SignalFlux — built-in example projects (D38).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Built in code until project files exist (SF-010). Each example doubles as a
// golden-log fixture for the cross-platform determinism gate (D25). The full
// set of six from D38 grows as features land.

#pragma once

#include "domain/Project.h"

#include <optional>
#include <string_view>
#include <vector>

namespace signalflux::examples {

/// Names of all built-in examples, in a stable order.
[[nodiscard]] std::vector<std::string_view> names();

/// The example with this name, or nullopt.
[[nodiscard]] std::optional<ProjectDocument> make(std::string_view name);

} // namespace signalflux::examples
