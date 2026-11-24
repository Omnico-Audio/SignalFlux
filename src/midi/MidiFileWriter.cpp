// SignalFlux — Standard MIDI File export (D28).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "midi/MidiFileWriter.h"

#include "midi/MidiBytes.h"

#include <algorithm>
#include <array>
#include <map>

namespace signalflux::midi {

namespace {

void be16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>((v >> s) & 0xFF));
}

void varLen(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::array<std::uint8_t, 5> buf{};
    std::size_t n = 0;
    buf[n++] = static_cast<std::uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = static_cast<std::uint8_t>(0x80 | (v & 0x7F));
    while (n > 0) out.push_back(buf[--n]);
}

void metaText(std::vector<std::uint8_t>& track, std::uint8_t type, const std::string& text) {
    varLen(track, 0);
    track.push_back(0xFF);
    track.push_back(type);
    varLen(track, static_cast<std::uint32_t>(text.size()));
    track.insert(track.end(), text.begin(), text.end());
}

void appendTrack(std::vector<std::uint8_t>& file, std::vector<std::uint8_t> track) {
    varLen(track, 0); // End of Track
    track.push_back(0xFF);
    track.push_back(0x2F);
    track.push_back(0x00);
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    be32(file, static_cast<std::uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());
}

} // namespace

std::vector<std::uint8_t> writeMidiFile(const std::vector<engine::EngineEvent>& events, const MidiFileInfo& info) {
    using T = engine::EngineEventType;
    std::int64_t first = -1;
    std::map<int, std::vector<const engine::EngineEvent*>> byChannel; // ordered by channel
    for (const auto& e : events) {
        if (e.type != T::NoteOn && e.type != T::NoteOff && e.type != T::ControlChange && e.type != T::PitchBend) continue;
        if (first < 0) first = e.tick.value();
        byChannel[e.channel.value()].push_back(&e);
    }

    std::vector<std::uint8_t> file{'M', 'T', 'h', 'd'};
    be32(file, 6);
    be16(file, 1);                                                 // type 1
    be16(file, static_cast<std::uint32_t>(1 + byChannel.size())); // tempo track + channel tracks
    be16(file, static_cast<std::uint32_t>(kTicksPerQuarter));

    std::vector<std::uint8_t> tempoTrack;
    metaText(tempoTrack, 0x03, info.projectName.empty() ? "SignalFlux" : info.projectName); // track name
    metaText(tempoTrack, 0x01, "SignalFlux seed " + std::to_string(info.seed));               // text
    const auto usPerQuarter = static_cast<std::uint32_t>(60'000'000'000ll / info.tempo.value());
    varLen(tempoTrack, 0);
    tempoTrack.insert(tempoTrack.end(), {0xFF, 0x51, 0x03});
    tempoTrack.push_back(static_cast<std::uint8_t>((usPerQuarter >> 16) & 0xFF));
    tempoTrack.push_back(static_cast<std::uint8_t>((usPerQuarter >> 8) & 0xFF));
    tempoTrack.push_back(static_cast<std::uint8_t>(usPerQuarter & 0xFF));
    appendTrack(file, std::move(tempoTrack));

    std::array<MidiMessage, kMaxMessagesPerEvent> msgs{};
    for (const auto& [channel, list] : byChannel) {
        std::vector<std::uint8_t> track;
        metaText(track, 0x03, "Channel " + std::to_string(channel));
        std::int64_t last = first;
        for (const auto* e : list) {
            const auto n = encode(*e, false, msgs);
            for (std::size_t i = 0; i < n; ++i) {
                varLen(track, static_cast<std::uint32_t>(e->tick.value() - last));
                last = e->tick.value();
                track.insert(track.end(), msgs[i].bytes.begin(), msgs[i].bytes.begin() + msgs[i].size);
            }
        }
        appendTrack(file, std::move(track));
    }
    return file;
}

} // namespace signalflux::midi
