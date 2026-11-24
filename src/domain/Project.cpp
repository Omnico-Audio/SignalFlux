// SignalFlux — project document model (task SF-002).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Project.h"

namespace signalflux {

ProjectDocument makeDefaultProject() {
    ProjectDocument doc;
    doc.plots.push_back(Plot{});
    return doc;
}

} // namespace signalflux
