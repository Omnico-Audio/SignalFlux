// SignalFlux — headless renderer (DECISIONS D25, D28).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Runs the engine with no GUI, audio, or MIDI devices and prints the canonical
// event log. Project files arrive with SF-010; until then it renders the
// built-in examples.
//
//   signalflux-render --version
//   signalflux-render --self-check
//   signalflux-render --list
//   signalflux-render --example NAME [--beats N]
//   signalflux-render --project FILE [--beats N]
//   signalflux-render --export-examples DIR

#include "domain/Examples.h"
#include "domain/Validation.h"
#include "engine/EventLog.h"
#include "persistence/ProjectSerializer.h"

#include <charconv>
#include <filesystem>
#include <cstdio>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

int printUsage() {
    std::fputs("usage: signalflux-render --version | --self-check | --list\n"
               "       signalflux-render --example NAME [--beats N]\n"
               "       signalflux-render --project FILE [--beats N]\n"
               "       signalflux-render --export-examples DIR\n",
               stderr);
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

int renderDocument(const signalflux::ProjectDocument& doc, std::int64_t beats);

int renderExample(std::string_view name, std::int64_t beats) {
    const auto doc = signalflux::examples::make(name);
    if (!doc) {
        std::fprintf(stderr, "unknown example '%.*s' (try --list)\n", static_cast<int>(name.size()), name.data());
        return 1;
    }
    return renderDocument(*doc, beats);
}

int renderProject(const char* path, std::int64_t beats) {
    const auto loaded = signalflux::persistence::loadFromFile(path);
    for (const auto& w : loaded.warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    if (!loaded.ok()) {
        for (const auto& e : loaded.errors) std::fprintf(stderr, "error: %s\n", e.c_str());
        return 1;
    }
    return renderDocument(*loaded.document, beats);
}

int exportExamples(const char* dir) {
    for (const auto name : signalflux::examples::names()) {
        const auto path = std::filesystem::path(dir) / (std::string(name) + signalflux::persistence::kFileExtension);
        if (const auto err = signalflux::persistence::saveToFile(*signalflux::examples::make(name), path)) {
            std::fprintf(stderr, "error: %s\n", err->c_str());
            return 1;
        }
        std::printf("%s\n", path.string().c_str());
    }
    return 0;
}

int renderDocument(const signalflux::ProjectDocument& doc, std::int64_t beats) {
    const auto log = signalflux::engine::renderCanonicalLog(doc, signalflux::MusicalTick{beats * signalflux::kTicksPerQuarter});
    if (log.empty()) {
        std::fputs("example failed validation\n", stderr);
        return 1;
    }
    std::fwrite(log.data(), 1, log.size(), stdout); // exact bytes, '\n' line endings
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Text-mode stdout would turn '\n' into "\r\n" and break byte-identical logs (D25).
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2) return printUsage();
    const std::string_view cmd{argv[1]};

    if (cmd == "--version" && argc == 2) {
        std::printf("signalflux-render %s\n", SIGNALFLUX_VERSION_STRING);
        return 0;
    }
    if (cmd == "--self-check" && argc == 2) return selfCheck();
    if (cmd == "--list" && argc == 2) {
        for (const auto n : signalflux::examples::names()) std::printf("%.*s\n", static_cast<int>(n.size()), n.data());
        return 0;
    }
    if (cmd == "--export-examples" && argc == 3) return exportExamples(argv[2]);
    if ((cmd == "--example" || cmd == "--project") && (argc == 3 || argc == 5)) {
        std::int64_t beats = 16;
        if (argc == 5) {
            const std::string_view flag{argv[3]};
            const std::string_view value{argv[4]};
            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), beats);
            if (flag != "--beats" || ec != std::errc{} || ptr != value.data() + value.size() || beats < 1 || beats > 100000)
                return printUsage();
        }
        return cmd == "--example" ? renderExample(argv[2], beats) : renderProject(argv[2], beats);
    }
    return printUsage();
}
