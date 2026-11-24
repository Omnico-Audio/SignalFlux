// SignalFlux — headless renderer (DECISIONS D25, D28).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Current scope (SF-001/SF-002): reports its version and validates the default
// project. Rendering a project to a canonical event log and to .mid arrives
// with the scheduler (SF-003) and persistence (SF-010).

#include "domain/Validation.h"

#include <cstdio>
#include <string_view>

namespace {

int printUsage() {
    std::puts("usage: signalflux-render --version | --self-check");
    return 2;
}

int selfCheck() {
    const auto report = signalflux::validateProject(signalflux::makeDefaultProject());
    for (const auto& issue : report.issues)
        std::printf("%s %s %s: %s\n", issue.severity == signalflux::Severity::Error ? "error" : "warning",
                    std::string(signalflux::toString(issue.code)).c_str(), issue.path.c_str(), issue.message.c_str());
    std::printf("default project: %s\n", report.ok() ? "valid" : "INVALID");
    return report.ok() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return printUsage();
    const std::string_view arg{argv[1]};
    if (arg == "--version") {
        std::printf("signalflux-render %s\n", SIGNALFLUX_VERSION_STRING);
        return 0;
    }
    if (arg == "--self-check") return selfCheck();
    return printUsage();
}
